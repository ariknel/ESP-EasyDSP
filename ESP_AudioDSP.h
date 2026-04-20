#pragma once

// =============================================================================
// ESP_AudioDSP.h  –  Single include for the entire library.
// =============================================================================
//
//  Quick-start:
//
//    #include "ESP_AudioDSP.h"
//
//    AudioDSP dsp;
//
//    void setup() {
//        dsp.begin(44100, 16);
//        dsp.setInput(INPUT_I2S_ADC);     // PCM1808 on I2S_NUM_1
//        dsp.setOutput(OUTPUT_PCM5102);   // PCM5102A on I2S_NUM_0
//        dsp.addLowPass(1000.f, 0.707f);
//        dsp.start();
//    }
//
//    void loop() { /* DSP runs on Core 1 automatically */ }
//
// =============================================================================

#include "AudioConfig.h"
#include "src/AudioTypes.h"
#include "src/CoeffCalc.h"
#include "src/FilterChain.h"
#include "src/AudioIO.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char* ADSP_TAG = "AudioDSP";

class AudioDSP {
public:

    // =========================================================================
    //  Setup
    // =========================================================================

    /**
     * Configure sample rate and bit depth.
     * Call before setInput / setOutput / start().
     *
     * @param sampleRate  Recommended: 44100 or 48000
     * @param bitDepth    16 (default) or 32
     */
    void begin(uint32_t sampleRate = 44100, uint8_t bitDepth = 16) {
        _sampleRate = sampleRate;
        _bitDepth   = bitDepth;
    }

    /**
     * Select the audio input source.
     *
     *   INPUT_I2S_ADC      – External I2S ADC (PCM1808) on I2S_NUM_1
     *                        Pins: PIN_I2S_IN_BCK / _WS / _DATA in AudioConfig.h
     *
     *   INPUT_INTERNAL_ADC – ESP32 built-in ADC on PIN_ADC_IN (GPIO36 default)
     *                        Lower quality (~60 dB SNR), but no extra hardware.
     */
    void setInput(InputSource src) { _inputSrc = src; }

    /**
     * Select the audio output.
     *
     *   OUTPUT_PCM5102 – PCM5102A I2S DAC on I2S_NUM_0
     *                    Pins: PIN_I2S_OUT_BCK / _WS / _DATA in AudioConfig.h
     */
    void setOutput(OutputDest dst) { _outputDst = dst; }

    /**
     * Set a linear output gain applied after all filters.
     * Default 1.0 (unity).  Useful to compensate for filter level loss.
     *
     * @param gain  Linear multiplier.  0.5 = −6 dB, 2.0 = +6 dB.
     */
    void setOutputGain(float gain) { _outputGain = gain; }

    // =========================================================================
    //  Filter chain
    //  – Call any time, before or after start().  Thread-safe.
    //  – Stages are applied in the order they were added.
    //  – Each add*() returns a stage index for later update/remove.
    // =========================================================================

    /**
     * Add a low-pass filter (attenuates above cutoff).
     *
     * @param freq  Cutoff frequency in Hz
     * @param Q     Resonance.  0.707 = Butterworth (maximally flat, default)
     *              Higher Q adds a resonant peak just below cutoff.
     *
     * Returns stage index, or -1 if the chain is full.
     *
     * Example:
     *   dsp.addLowPass(800.f);           // Muffled effect
     *   dsp.addLowPass(200.f, 1.5f);     // Boomy bass with resonance
     */
    int addLowPass(float freq, float Q = 0.707f) {
        return addStage(CoeffCalc::lowPass(freq, Q, _sampleRate));
    }

    /**
     * Add a high-pass filter (attenuates below cutoff).
     *
     * @param freq  Cutoff frequency in Hz
     * @param Q     Resonance.  0.707 = Butterworth (default)
     *
     * Example:
     *   dsp.addHighPass(80.f);           // Remove rumble / DC offset
     *   dsp.addHighPass(300.f, 0.9f);    // Telephone effect
     */
    int addHighPass(float freq, float Q = 0.707f) {
        return addStage(CoeffCalc::highPass(freq, Q, _sampleRate));
    }

    /**
     * Add a band-pass filter (passes a range around center frequency).
     *
     * @param freq         Center frequency in Hz
     * @param bandwidthHz  Width of the passband in Hz
     *
     * Example:
     *   dsp.addBandPass(1000.f, 500.f);  // Passes 750–1250 Hz
     */
    int addBandPass(float freq, float bandwidthHz) {
        return addStage(CoeffCalc::bandPass(freq, bandwidthHz, _sampleRate));
    }

    /**
     * Add a notch filter (narrow null at one frequency).
     * Primary use: remove mains hum.
     *
     * @param freq  Frequency to remove in Hz (e.g. 50.f or 60.f)
     *
     * Example:
     *   dsp.addNotch(50.f);   // EU 230V mains hum
     *   dsp.addNotch(60.f);   // US 120V mains hum
     */
    int addNotch(float freq) {
        return addStage(CoeffCalc::notch(freq, _sampleRate));
    }

    /**
     * Add a parametric (peaking) EQ band.
     *
     * @param freq    Center frequency in Hz
     * @param Q       Bandwidth (higher = narrower band)
     * @param dBgain  Boost (+) or cut (−) in dB.  Range: ±24 dB
     *
     * Example:
     *   dsp.addPeakEQ(3000.f, 1.4f, +3.f);   // Presence boost
     *   dsp.addPeakEQ(200.f,  1.0f, -4.f);   // Cut muddiness
     */
    int addPeakEQ(float freq, float Q, float dBgain) {
        return addStage(CoeffCalc::peakEQ(freq, Q, dBgain, _sampleRate));
    }

    /**
     * Add a low-shelf filter (boosts/cuts below the shelf frequency).
     *
     * @param freq    Shelf frequency in Hz
     * @param dBgain  Gain below shelf in dB  (+4 = bass boost, −6 = bass cut)
     *
     * Example:
     *   dsp.addLowShelf(200.f, +4.f);   // Bass boost
     *   dsp.addLowShelf(100.f, -6.f);   // Cut sub-bass
     */
    int addLowShelf(float freq, float dBgain) {
        return addStage(CoeffCalc::lowShelf(freq, dBgain, _sampleRate));
    }

    /**
     * Add a high-shelf filter (boosts/cuts above the shelf frequency).
     *
     * @param freq    Shelf frequency in Hz
     * @param dBgain  Gain above shelf in dB  (+3 = treble boost, −4 = treble cut)
     *
     * Example:
     *   dsp.addHighShelf(8000.f, +2.f);   // Air boost / brightness
     *   dsp.addHighShelf(6000.f, -4.f);   // Tame harsh treble
     */
    int addHighShelf(float freq, float dBgain) {
        return addStage(CoeffCalc::highShelf(freq, dBgain, _sampleRate));
    }

    // -------------------------------------------------------------------------
    //  Live update – change filter parameters without audio dropout
    //  Use the index returned by the corresponding add*() call.
    // -------------------------------------------------------------------------

    void updateLowPass (int idx, float freq, float Q = 0.707f)
        { updateStage(idx, CoeffCalc::lowPass (freq, Q, _sampleRate)); }

    void updateHighPass(int idx, float freq, float Q = 0.707f)
        { updateStage(idx, CoeffCalc::highPass(freq, Q, _sampleRate)); }

    void updateBandPass(int idx, float freq, float bwHz)
        { updateStage(idx, CoeffCalc::bandPass(freq, bwHz, _sampleRate)); }

    void updateNotch   (int idx, float freq)
        { updateStage(idx, CoeffCalc::notch   (freq, _sampleRate)); }

    void updatePeakEQ  (int idx, float freq, float Q, float dBgain)
        { updateStage(idx, CoeffCalc::peakEQ  (freq, Q, dBgain, _sampleRate)); }

    void updateLowShelf(int idx, float freq, float dBgain)
        { updateStage(idx, CoeffCalc::lowShelf(freq, dBgain, _sampleRate)); }

    void updateHighShelf(int idx, float freq, float dBgain)
        { updateStage(idx, CoeffCalc::highShelf(freq, dBgain, _sampleRate)); }

    /**
     * Remove all filter stages. Audio continues as pass-through.
     */
    void clearFilters() {
        lock();
        _chain.clear();
        unlock();
    }

    int filterCount() const { return _chain.stageCount(); }

    // =========================================================================
    //  Start / stop
    // =========================================================================

    /**
     * Initialise hardware and start the background DSP task on Core 1.
     * Call after begin(), setInput(), setOutput(), and your initial filters.
     *
     * Returns true on success.  On failure, check Serial for error messages.
     */
    bool start() {
        if (!_io.beginOutput(_sampleRate, _bitDepth)) {
            ESP_LOGE(ADSP_TAG, "I2S output init failed");
            return false;
        }

        if (_inputSrc == INPUT_I2S_ADC) {
            if (!_io.beginI2SInput(_sampleRate, _bitDepth)) {
                ESP_LOGE(ADSP_TAG, "I2S input init failed");
                return false;
            }
        } else {
            _io.beginInternalADC(ADC1_CHANNEL_0);
        }

        _mutex   = xSemaphoreCreateMutex();
        _running = true;

        xTaskCreatePinnedToCore(
            _dspTask,
            "AudioDSP",
            DSP_TASK_STACK,
            this,
            DSP_TASK_PRIORITY,
            &_taskHandle,
            DSP_TASK_CORE
        );

        ESP_LOGI(ADSP_TAG, "Running. SR=%lu bit=%d in=%s stages=%d",
                 (unsigned long)_sampleRate, _bitDepth,
                 _inputSrc == INPUT_I2S_ADC ? "I2S" : "intADC",
                 _chain.stageCount());
        return true;
    }

    /**
     * Stop the DSP task and release all I2S resources.
     */
    void stop() {
        _running = false;
        vTaskDelay(pdMS_TO_TICKS(50));  // let the task finish its current buffer
        if (_taskHandle) { vTaskDelete(_taskHandle); _taskHandle = nullptr; }
        i2s_driver_uninstall(I2S_NUM_0);
        if (_inputSrc == INPUT_I2S_ADC) i2s_driver_uninstall(I2S_NUM_1);
        if (_mutex) { vSemaphoreDelete(_mutex); _mutex = nullptr; }
        _running = false;
    }

    bool isRunning() const { return _running; }

    /** Print a one-line status to Serial. */
    void printStatus() {
        Serial.printf("[AudioDSP] running=%d stages=%d sr=%lu bd=%d gain=%.2f\n",
                      _running, _chain.stageCount(),
                      (unsigned long)_sampleRate, _bitDepth, _outputGain);
    }

private:
    AudioIO      _io;
    FilterChain  _chain;

    uint32_t    _sampleRate  = 44100;
    uint8_t     _bitDepth    = 16;
    InputSource _inputSrc    = INPUT_I2S_ADC;
    OutputDest  _outputDst   = OUTPUT_PCM5102;
    float       _outputGain  = 1.0f;

    TaskHandle_t      _taskHandle = nullptr;
    SemaphoreHandle_t _mutex      = nullptr;
    volatile bool     _running    = false;

    static constexpr uint32_t BUF_FLOATS = DMA_BUF_LEN * 2;

    void lock()   { if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY); }
    void unlock() { if (_mutex) xSemaphoreGive(_mutex); }

    int addStage(const BiquadCoeffs& c) {
        lock();
        int idx = _chain.addStage(c);
        unlock();
        if (idx < 0) ESP_LOGW(ADSP_TAG, "Chain full (max %d stages)", MAX_BIQUAD_STAGES);
        return idx;
    }

    void updateStage(int idx, const BiquadCoeffs& c) {
        lock();
        _chain.updateStage(idx, c);
        unlock();
    }

    void _loop() {
        float buf[BUF_FLOATS];
        while (_running) {
            _io.read(buf, DMA_BUF_LEN);

            lock();
            _chain.process(buf, DMA_BUF_LEN);
            unlock();

            if (_outputGain != 1.0f)
                for (uint32_t i = 0; i < BUF_FLOATS; ++i)
                    buf[i] *= _outputGain;

            _io.write(buf, DMA_BUF_LEN);
        }
    }

    static void _dspTask(void* arg) {
        static_cast<AudioDSP*>(arg)->_loop();
        vTaskDelete(nullptr);
    }
};
