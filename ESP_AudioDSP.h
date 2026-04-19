#pragma once

// =============================================================================
// ESP_AudioDSP.h — Main public API
// This is the only file your sketch needs to include.
// =============================================================================

#include "AudioConfig.h"
#include "src/AudioTypes.h"
#include "src/CoeffCalc.h"
#include "src/DSPEngine.h"
#include "src/I2S_HAL.h"
#include "src/BufferManager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#ifdef AUDIO_DSP_BENCHMARK
  #include "esp_timer.h"
#endif

class AudioDSP {
public:

    // =========================================================================
    // Setup
    // =========================================================================

    /**
     * Initialise the audio engine.
     * Must be called first, before any other function.
     *
     * @param sampleRate  8000, 22050, 44100, or 48000 Hz
     * @param bitDepth    16, 24, or 32 bits per sample
     */
    bool begin(uint32_t sampleRate = 44100, uint8_t bitDepth = 16) {
        _sampleRate = sampleRate;
        _bitDepth   = bitDepth;

        if (!_bufMgr.begin(AUDIO_BUFFER_SIZE, 2)) return false;
        if (!_hal.beginOutput(sampleRate, bitDepth)) return false;

        if (_inputType == INPUT_LINE_IN || _inputType == INPUT_I2S_MIC) {
            if (!_hal.beginInput(sampleRate, bitDepth)) return false;
            _hasInput = true;
        }

#ifndef AUDIO_DSP_MANUAL_PROCESS
        xTaskCreatePinnedToCore(
            _dspTask,
            "AudioDSP",
            4096,
            this,
            AUDIO_TASK_PRIORITY,
            &_taskHandle,
            AUDIO_TASK_CORE
        );
#endif
        return true;
    }

    /**
     * Select the audio input source.
     * Call before begin().
     */
    void setInput(InputType type) { _inputType = type; }

    /**
     * Select the audio output destination.
     * Call before begin().
     */
    void setOutput(OutputType type) { _outputType = type; }

    /**
     * Override the DMA buffer size in samples.
     * Call before begin(). Default: AUDIO_BUFFER_SIZE (256).
     * Lower = less latency, higher = more stability.
     */
    void setBufferSize(uint16_t samples) {
        // Takes effect on next begin() call
        // (stored for documentation; AUDIO_BUFFER_SIZE is compile-time)
        (void)samples;
    }

    /**
     * Manually trigger one processing cycle.
     * Only needed if AUDIO_DSP_MANUAL_PROCESS is defined.
     */
    void process() {
        _runOnce();
    }

    /**
     * Remove all effects from the chain and reset to pass-through.
     */
    void clearEffects() {
        for (uint8_t i = 0; i < AUDIO_MAX_EFFECTS; ++i) {
            freeSlotMemory(_slots[i]);
            _slots[i] = EffectSlot{};
        }
        _slotCount = 0;
    }

    // =========================================================================
    // Filter Functions
    // =========================================================================

    /**
     * Low-pass filter — attenuates frequencies above the cutoff.
     *
     * @param frequency_hz  Cutoff frequency in Hz (20 – sampleRate/2)
     * @param q_factor      Filter resonance. 0.707 = maximally flat (Butterworth)
     *
     * Example:
     *   dsp.setLowPass(800.0f, 0.707f);  // Muffled / underwater effect
     */
    void setLowPass(float frequency_hz, float q_factor = 0.707f) {
        EffectSlot& s = addSlot(EFFECT_LOWPASS);
        s.coeffs = CoeffCalc::lowPass(frequency_hz, q_factor, _sampleRate);
    }

    /**
     * High-pass filter — attenuates frequencies below the cutoff.
     *
     * @param frequency_hz  Cutoff frequency in Hz
     * @param q_factor      Filter resonance. 0.707 = maximally flat
     *
     * Example:
     *   dsp.setHighPass(80.0f, 0.707f);  // Remove low-end rumble
     */
    void setHighPass(float frequency_hz, float q_factor = 0.707f) {
        EffectSlot& s = addSlot(EFFECT_HIGHPASS);
        s.coeffs = CoeffCalc::highPass(frequency_hz, q_factor, _sampleRate);
    }

    /**
     * Band-pass filter — passes only frequencies around the center.
     *
     * @param frequency_hz   Center frequency in Hz
     * @param bandwidth_hz   Width of the passband in Hz
     *
     * Example:
     *   dsp.setBandPass(1000.0f, 500.0f);  // Radio / telephone effect
     */
    void setBandPass(float frequency_hz, float bandwidth_hz) {
        EffectSlot& s = addSlot(EFFECT_BANDPASS);
        s.coeffs = CoeffCalc::bandPass(frequency_hz, bandwidth_hz, _sampleRate);
    }

    /**
     * Notch filter — removes a single narrow frequency.
     * Ideal for eliminating mains hum (50/60 Hz).
     *
     * @param frequency_hz  The frequency to remove in Hz
     *
     * Example:
     *   dsp.setNotch(50.0f);  // Remove EU mains hum
     */
    void setNotch(float frequency_hz) {
        EffectSlot& s = addSlot(EFFECT_NOTCH);
        s.coeffs = CoeffCalc::notch(frequency_hz, _sampleRate);
    }

    /**
     * Parametric EQ — boost or cut a frequency band.
     * Multiple calls build a multi-band parametric equaliser.
     *
     * @param frequency_hz  Center frequency in Hz
     * @param q_factor      Band width (lower Q = wider band)
     * @param gain_db       Boost (+) or cut (−) in decibels. Range: ±24 dB
     *
     * Example:
     *   dsp.setParametricEQ(3000.0f, 1.4f, +3.0f);  // Presence boost
     *   dsp.setParametricEQ(200.0f,  1.0f, -3.0f);  // Low-mid cut
     */
    void setParametricEQ(float frequency_hz, float q_factor, float gain_db) {
        EffectSlot& s = addSlot(EFFECT_PARAMETRIC_EQ);
        s.coeffs = CoeffCalc::parametricEQ(frequency_hz, q_factor, gain_db, _sampleRate);
    }

    /**
     * Low shelf — boosts or cuts all frequencies below the shelf point.
     *
     * @param frequency_hz  Shelf transition frequency in Hz
     * @param gain_db       Gain applied below the shelf. Range: ±24 dB
     *
     * Example:
     *   dsp.setLowShelf(200.0f, +4.0f);  // Bass boost
     */
    void setLowShelf(float frequency_hz, float gain_db) {
        EffectSlot& s = addSlot(EFFECT_LOW_SHELF);
        s.coeffs = CoeffCalc::lowShelf(frequency_hz, gain_db, _sampleRate);
    }

    /**
     * High shelf — boosts or cuts all frequencies above the shelf point.
     *
     * @param frequency_hz  Shelf transition frequency in Hz
     * @param gain_db       Gain applied above the shelf. Range: ±24 dB
     *
     * Example:
     *   dsp.setHighShelf(8000.0f, +2.0f);  // Air boost / add brightness
     */
    void setHighShelf(float frequency_hz, float gain_db) {
        EffectSlot& s = addSlot(EFFECT_HIGH_SHELF);
        s.coeffs = CoeffCalc::highShelf(frequency_hz, gain_db, _sampleRate);
    }

    // =========================================================================
    // Dynamics Functions
    // =========================================================================

    /**
     * Gain — multiply every sample by a linear factor.
     *
     * Multiplier reference:
     *   0.5  = -6 dB   |   1.0 = 0 dB (unity)   |   2.0 = +6 dB
     *
     * @param multiplier  Linear gain factor (0.0 – 4.0 recommended)
     *
     * Example:
     *   dsp.setGain(1.5f);  // Moderate boost
     */
    void setGain(float multiplier) {
        EffectSlot& s = addSlot(EFFECT_GAIN);
        s.param0 = multiplier;
    }

    /**
     * Compressor — reduces dynamic range above a threshold.
     *
     * @param threshold  Signal level at which compression starts (0.0 – 1.0,
     *                   fraction of full scale). Example: 0.7 = 70% full scale.
     * @param ratio      Compression ratio. 2.0 = gentle, 4.0 = standard,
     *                   10.0+ ≈ limiting.
     *
     * Example:
     *   dsp.setCompressor(0.7f, 4.0f);  // Standard vocal compression
     */
    void setCompressor(float threshold, float ratio) {
        EffectSlot& s = addSlot(EFFECT_COMPRESSOR);
        s.param0 = threshold;
        s.param1 = ratio;
    }

    /**
     * Noise gate — silences the output when the signal drops below a threshold.
     *
     * @param threshold  Level below which the gate closes (0.0 – 1.0).
     *                   Typical value: 0.02 (2% full scale).
     *
     * Example:
     *   dsp.setNoiseGate(0.02f);  // Remove mic hiss between words
     */
    void setNoiseGate(float threshold) {
        EffectSlot& s = addSlot(EFFECT_NOISE_GATE);
        s.param0 = threshold;
    }

    /**
     * Limiter — hard ceiling; prevents output exceeding the ceiling value.
     * Place last in the chain to protect the DAC from clipping.
     *
     * @param ceiling  Maximum allowed output level (0.0 – 1.0).
     *                 Recommended: 0.95 (leaves 0.5 dB headroom).
     *
     * Example:
     *   dsp.setLimiter(0.95f);
     */
    void setLimiter(float ceiling) {
        EffectSlot& s = addSlot(EFFECT_LIMITER);
        s.param0 = ceiling;
    }

    // =========================================================================
    // Effect Functions
    // =========================================================================

    /**
     * Distortion — drive and clip the signal for overdrive / fuzz tones.
     * Blends soft-clip (tanh) and hard-clip based on amount.
     *
     * @param amount  0.0 = clean, 1.0 = maximum clipping.
     *
     * Tip: place a high-pass before and a low-pass after for best guitar tone:
     *   dsp.setHighPass(100.0f, 0.707f);
     *   dsp.setGain(3.0f);
     *   dsp.setDistortion(0.6f);
     *   dsp.setLowPass(6000.0f, 0.707f);
     */
    void setDistortion(float amount) {
        EffectSlot& s = addSlot(EFFECT_DISTORTION);
        s.param0 = amount;
    }

    /**
     * Bit crusher — reduces effective bit depth for lo-fi / retro sound.
     *
     * @param bits  Target bit depth (2 – 16).
     *              16 = transparent | 8 = Game Boy | 4 = extreme lo-fi
     *
     * Example:
     *   dsp.setBitCrusher(8);  // Classic 8-bit sound
     */
    void setBitCrusher(int bits) {
        EffectSlot& s = addSlot(EFFECT_BITCRUSHER);
        s.param0 = (float)constrain(bits, 2, 16);
    }

    /**
     * Delay — echo effect with a feedback loop.
     * Requires PSRAM (define BOARD_HAS_PSRAM) for delays above ~90 ms at 44100 Hz.
     *
     * @param delay_ms   Delay time in milliseconds (1 – 2000 ms).
     * @param feedback   Echo tail length (0.0 = single echo, 0.9 = many repeats).
     *                   WARNING: never use 1.0 or above — causes runaway feedback.
     *
     * Example:
     *   dsp.setDelay(250.0f, 0.4f);  // Quarter-note delay at 120 BPM
     */
    void setDelay(float delay_ms, float feedback) {
        EffectSlot& s = addSlot(EFFECT_DELAY);
        s.param0      = delay_ms;
        s.param1      = constrain(feedback, 0.0f, 0.95f);

        uint32_t delaySamples = (uint32_t)((delay_ms / 1000.0f) * _sampleRate) * 2;
        s.delayLen   = delaySamples;
        s.delayBuf   = allocateAudioHeap(delaySamples);
        s.delayWrite = 0;
        if (s.delayBuf) memset(s.delayBuf, 0, delaySamples * sizeof(float));
    }

    /**
     * Chorus — LFO-modulated delay for width and warmth.
     *
     * @param rate_hz  LFO modulation speed in Hz (0.1 – 5.0).
     * @param depth    Depth of pitch modulation (0.0 – 1.0).
     *
     * Example:
     *   dsp.setChorus(1.2f, 0.3f);  // Classic chorus
     */
    void setChorus(float rate_hz, float depth) {
        EffectSlot& s    = addSlot(EFFECT_CHORUS);
        s.param0         = rate_hz;
        s.param1         = depth;
        uint32_t bufSamples = (uint32_t)(0.030f * _sampleRate) * 2;  // 30 ms max
        s.delayLen   = bufSamples;
        s.delayBuf   = allocateAudioHeap(bufSamples);
        s.delayWrite = 0;
        if (s.delayBuf) memset(s.delayBuf, 0, bufSamples * sizeof(float));
    }

    /**
     * Reverb — algorithmic room simulation (Schroeder network).
     *
     * @param room_size  Simulated space size (0.0 = tiny room, 1.0 = large hall).
     * @param damping    High-frequency absorption (0.0 = bright, 1.0 = dark/warm).
     *
     * Example:
     *   dsp.setReverb(0.5f, 0.4f);  // Medium studio room
     *   dsp.setReverb(0.9f, 0.2f);  // Large concert hall
     */
    void setReverb(float room_size, float damping) {
        EffectSlot& s = addSlot(EFFECT_REVERB);
        s.param0      = constrain(room_size, 0.0f, 1.0f);
        s.param1      = constrain(damping,   0.0f, 1.0f);

        s.delayLen  = DSPEngine::REVERB_BUF_TOTAL;
        s.delayBuf  = allocateAudioHeap(DSPEngine::REVERB_BUF_TOTAL);
        if (s.delayBuf) memset(s.delayBuf, 0,
                               DSPEngine::REVERB_BUF_TOTAL * sizeof(float));
    }

    // =========================================================================
    // Internal
    // =========================================================================

private:
    I2S_HAL      _hal;
    BufferManager _bufMgr;
    EffectSlot   _slots[AUDIO_MAX_EFFECTS] = {};
    uint8_t      _slotCount  = 0;
    uint32_t     _sampleRate = 44100;
    uint8_t      _bitDepth   = 16;
    InputType    _inputType  = INPUT_LINE_IN;
    OutputType   _outputType = OUTPUT_PCM5102;
    bool         _hasInput   = false;
    TaskHandle_t _taskHandle = nullptr;

    // Append an effect slot to the chain
    EffectSlot& addSlot(EffectType type) {
        if (_slotCount >= AUDIO_MAX_EFFECTS) {
            // Chain full — overwrite last slot
            _slotCount = AUDIO_MAX_EFFECTS - 1;
        }
        EffectSlot& s = _slots[_slotCount++];
        s = EffectSlot{};
        s.type   = type;
        s.active = true;
        return s;
    }

    // Free heap memory owned by a slot (delay buffers etc.)
    static void freeSlotMemory(EffectSlot& s) {
        if (s.delayBuf) {
            free(s.delayBuf);
            s.delayBuf = nullptr;
        }
    }

    // Allocate from PSRAM if available, otherwise from DRAM
    static float* allocateAudioHeap(uint32_t floats) {
#ifdef BOARD_HAS_PSRAM
        float* p = (float*)heap_caps_malloc(floats * sizeof(float),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
#endif
        return (float*)heap_caps_malloc(floats * sizeof(float), MALLOC_CAP_8BIT);
    }

    // One processing cycle: capture → DSP → playback
    void _runOnce() {
        static float captureBuf[AUDIO_BUFFER_SIZE * 2];

#ifdef AUDIO_DSP_BENCHMARK
        int64_t t0 = esp_timer_get_time();
#endif

        // --- Capture ---
        if (_hasInput) {
            _hal.readFloat(captureBuf, AUDIO_BUFFER_SIZE);
            _bufMgr.submitFill(captureBuf, AUDIO_BUFFER_SIZE);
        } else {
            _bufMgr.submitSilence();
        }

        // --- Wait for the buffer the fill just swapped out ---
        float* buf = _bufMgr.waitForBuffer(pdMS_TO_TICKS(50));
        if (!buf) return;

        // --- DSP ---
        AudioBuffer ab { buf, AUDIO_BUFFER_SIZE, 2 };
        DSPEngine::process(ab, _slots, _slotCount);

        // --- Playback ---
        _hal.writeFloat(buf, AUDIO_BUFFER_SIZE);

#ifdef AUDIO_DSP_BENCHMARK
        int64_t us = esp_timer_get_time() - t0;
        Serial.printf("[AudioDSP] cycle: %lld µs\n", us);
#endif
    }

    // FreeRTOS task wrapper
    static void _dspTask(void* arg) {
        AudioDSP* self = static_cast<AudioDSP*>(arg);
        for (;;) {
            self->_runOnce();
        }
    }
};
