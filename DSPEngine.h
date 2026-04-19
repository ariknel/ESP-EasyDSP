#pragma once
#include <stdint.h>
#include <math.h>
#include "AudioTypes.h"

// esp-dsp provides SIMD-accelerated biquad on ESP32
#include "dsps_biquad.h"
#include "dsps_biquad_gen.h"

// =============================================================================
// DSPEngine.h — Processes an AudioBuffer through the full effect chain.
//
// Each call to process() iterates the effect list and applies every active
// slot in order. Biquad filters use dsps_biquad_f32() (esp-dsp SIMD).
// All other effects are implemented as tight float loops.
// =============================================================================

class DSPEngine {
public:

    // Run the full effect chain on a buffer of interleaved float samples.
    // slots[] and slotCount come from the AudioDSP effect chain.
    static void process(AudioBuffer& buf, EffectSlot* slots, uint8_t slotCount) {
        for (uint8_t i = 0; i < slotCount; ++i) {
            if (!slots[i].active) continue;
            applySlot(buf, slots[i]);
        }
    }

private:

    // Dispatch to the correct processor for each effect type
    static void applySlot(AudioBuffer& buf, EffectSlot& slot) {
        switch (slot.type) {
            case EFFECT_LOWPASS:
            case EFFECT_HIGHPASS:
            case EFFECT_BANDPASS:
            case EFFECT_NOTCH:
            case EFFECT_PARAMETRIC_EQ:
            case EFFECT_LOW_SHELF:
            case EFFECT_HIGH_SHELF:
                applyBiquad(buf, slot);
                break;

            case EFFECT_GAIN:        applyGain(buf, slot);        break;
            case EFFECT_COMPRESSOR:  applyCompressor(buf, slot);  break;
            case EFFECT_NOISE_GATE:  applyNoiseGate(buf, slot);   break;
            case EFFECT_LIMITER:     applyLimiter(buf, slot);     break;
            case EFFECT_DISTORTION:  applyDistortion(buf, slot);  break;
            case EFFECT_BITCRUSHER:  applyBitCrusher(buf, slot);  break;
            case EFFECT_DELAY:       applyDelay(buf, slot);       break;
            case EFFECT_CHORUS:      applyChorus(buf, slot);      break;
            case EFFECT_REVERB:      applyReverb(buf, slot);      break;
            default: break;
        }
    }

    // -------------------------------------------------------------------------
    // Biquad IIR — accelerated by dsps_biquad_f32 (esp-dsp)
    // Coefficients are pre-calculated by CoeffCalc and stored in the slot.
    // -------------------------------------------------------------------------
    static void applyBiquad(AudioBuffer& buf, EffectSlot& slot) {
        // esp-dsp expects coefficients as float[5]: {b0, b1, b2, a1, a2}
        float coeffs[5] = {
            slot.coeffs.b0,
            slot.coeffs.b1,
            slot.coeffs.b2,
            slot.coeffs.a1,
            slot.coeffs.a2
        };
        // esp-dsp state: float[2] = {w0, w1} (Direct Form II transposed)
        // We keep our own x/y history in BiquadState and map across.
        float w[2] = { slot.state.y1, slot.state.y2 };

        dsps_biquad_f32(buf.data, buf.data,
                        (int)(buf.frames * buf.channels),
                        coeffs, w);

        slot.state.y1 = w[0];
        slot.state.y2 = w[1];
    }

    // -------------------------------------------------------------------------
    // Gain
    // -------------------------------------------------------------------------
    static void applyGain(AudioBuffer& buf, EffectSlot& slot) {
        const float g = slot.param0;
        const uint32_t n = buf.frames * buf.channels;
        for (uint32_t i = 0; i < n; ++i)
            buf.data[i] *= g;
    }

    // -------------------------------------------------------------------------
    // Compressor — peak follower, gain computer, gain smoother
    // param0 = threshold (0-1), param1 = ratio
    // -------------------------------------------------------------------------
    static void applyCompressor(AudioBuffer& buf, EffectSlot& slot) {
        const float threshold = slot.param0;
        const float ratio     = slot.param1;
        const float attack    = 0.003f;   // ~3 ms
        const float release   = 0.100f;   // ~100 ms
        const uint32_t n      = buf.frames * buf.channels;

        float& envelope = slot.state.y1;  // reuse biquad state as envelope follower

        for (uint32_t i = 0; i < n; ++i) {
            float absVal = fabsf(buf.data[i]);
            // Envelope follower
            if (absVal > envelope)
                envelope += attack  * (absVal - envelope);
            else
                envelope += release * (absVal - envelope);

            // Gain computer
            float gain = 1.0f;
            if (envelope > threshold && ratio > 1.0f) {
                float over = envelope - threshold;
                float reduced = over / ratio;
                gain = (threshold + reduced) / envelope;
            }
            buf.data[i] *= gain;
        }
    }

    // -------------------------------------------------------------------------
    // Noise Gate — param0 = threshold
    // -------------------------------------------------------------------------
    static void applyNoiseGate(AudioBuffer& buf, EffectSlot& slot) {
        const float threshold = slot.param0;
        const uint32_t n      = buf.frames * buf.channels;
        float& envelope       = slot.state.y1;
        const float attack    = 0.001f;
        const float release   = 0.050f;

        for (uint32_t i = 0; i < n; ++i) {
            float absVal = fabsf(buf.data[i]);
            if (absVal > envelope)
                envelope += attack  * (absVal - envelope);
            else
                envelope += release * (absVal - envelope);

            if (envelope < threshold)
                buf.data[i] = 0.0f;
        }
    }

    // -------------------------------------------------------------------------
    // Limiter — param0 = ceiling
    // -------------------------------------------------------------------------
    static void applyLimiter(AudioBuffer& buf, EffectSlot& slot) {
        const float ceil = slot.param0;
        const uint32_t n = buf.frames * buf.channels;
        for (uint32_t i = 0; i < n; ++i) {
            if      (buf.data[i] >  ceil) buf.data[i] =  ceil;
            else if (buf.data[i] < -ceil) buf.data[i] = -ceil;
        }
    }

    // -------------------------------------------------------------------------
    // Distortion — soft / hard clip blend
    // param0 = amount (0-1)
    // -------------------------------------------------------------------------
    static void applyDistortion(AudioBuffer& buf, EffectSlot& slot) {
        const float amount = slot.param0;
        const float drive  = 1.0f + amount * 40.0f;  // up to 41x pre-gain
        const uint32_t n   = buf.frames * buf.channels;
        for (uint32_t i = 0; i < n; ++i) {
            float s = buf.data[i] * drive;
            // Soft clip via tanh, then blend toward hard clip with amount
            float soft = tanhf(s);
            float hard = (s >  1.0f) ?  1.0f :
                         (s < -1.0f) ? -1.0f : s;
            buf.data[i] = soft + amount * (hard - soft);
        }
    }

    // -------------------------------------------------------------------------
    // Bit crusher — param0 = bits (integer, 2-16)
    // -------------------------------------------------------------------------
    static void applyBitCrusher(AudioBuffer& buf, EffectSlot& slot) {
        const int    bits  = (int)slot.param0;
        const float  steps = powf(2.0f, (float)bits) - 1.0f;
        const uint32_t n   = buf.frames * buf.channels;
        for (uint32_t i = 0; i < n; ++i) {
            // Map -1..+1 → 0..steps, quantise, map back
            float normalised = (buf.data[i] + 1.0f) * 0.5f;
            normalised = roundf(normalised * steps) / steps;
            buf.data[i] = normalised * 2.0f - 1.0f;
        }
    }

    // -------------------------------------------------------------------------
    // Delay — ring buffer stored on heap (PSRAM if available)
    // param0 = delay_ms, param1 = feedback
    // -------------------------------------------------------------------------
    static void applyDelay(AudioBuffer& buf, EffectSlot& slot) {
        if (!slot.delayBuf) return;
        const float  feedback  = slot.param1;
        const uint32_t n       = buf.frames * buf.channels;
        const uint32_t len     = slot.delayLen;

        for (uint32_t i = 0; i < n; ++i) {
            float wet = slot.delayBuf[slot.delayWrite];
            slot.delayBuf[slot.delayWrite] = buf.data[i] + wet * feedback;
            slot.delayWrite = (slot.delayWrite + 1) % len;
            buf.data[i] = buf.data[i] * 0.7f + wet * 0.3f;  // wet/dry mix
        }
    }

    // -------------------------------------------------------------------------
    // Chorus — LFO-modulated delay
    // param0 = rate_hz, param1 = depth
    // state: y1 = LFO phase
    // -------------------------------------------------------------------------
    static void applyChorus(AudioBuffer& buf, EffectSlot& slot) {
        if (!slot.delayBuf) return;
        const float rate   = slot.param0;
        const float depth  = slot.param1;
        const float fs     = 44100.0f;   // fixed — chorus doesn't need exact fs
        const uint32_t n   = buf.frames * buf.channels;
        const uint32_t len = slot.delayLen;
        float& phase       = slot.state.y1;

        for (uint32_t i = 0; i < n; ++i) {
            // LFO
            float lfo   = sinf(phase);
            phase      += 2.0f * M_PI * rate / fs;
            if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

            // Modulated read offset (5–25ms range)
            uint32_t offset = (uint32_t)((0.015f + depth * 0.010f * lfo) * fs);
            uint32_t readIdx = (slot.delayWrite + len - offset) % len;

            slot.delayBuf[slot.delayWrite] = buf.data[i];
            slot.delayWrite = (slot.delayWrite + 1) % len;

            buf.data[i] = buf.data[i] * 0.7f + slot.delayBuf[readIdx] * 0.3f;
        }
    }

    // -------------------------------------------------------------------------
    // Reverb — Schroeder network (4 comb + 2 all-pass)
    // param0 = room_size (0-1), param1 = damping (0-1)
    // Comb and all-pass buffers packed into delayBuf sequentially.
    // -------------------------------------------------------------------------

    // Comb filter lengths (prime-ish, avoids flutter echoes)
    static constexpr uint32_t COMB_LEN[4] = { 1557, 1617, 1491, 1422 };
    static constexpr uint32_t AP_LEN[2]   = {  225,  556 };
    static constexpr uint32_t REVERB_BUF_TOTAL =
        1557 + 1617 + 1491 + 1422 + 225 + 556;  // = 6868 floats

    static void applyReverb(AudioBuffer& buf, EffectSlot& slot) {
        if (!slot.delayBuf) return;
        const float roomSize = slot.param0;
        const float damping  = slot.param1;
        const uint32_t n     = buf.frames * buf.channels;

        // Pointers into the packed buffer
        float* comb[4];
        float* ap[2];
        comb[0] = slot.delayBuf;
        for (int c = 1; c < 4; ++c) comb[c] = comb[c-1] + COMB_LEN[c-1];
        ap[0] = comb[3] + COMB_LEN[3];
        ap[1] = ap[0]   + AP_LEN[0];

        // Write heads stored in state
        uint32_t* wh = (uint32_t*)&slot.state;  // 4 floats = 4 uint32_t heads (8 avail)
        // Lay out: wh[0..3] = comb heads, wh[4..5] = ap heads
        // (BiquadState has 4 floats; we reinterpret as 4 uint32_t)
        // Use param2 as a tiny bitfield — not elegant but avoids extra heap.

        for (uint32_t i = 0; i < n; ++i) {
            float input = buf.data[i];
            float out   = 0.0f;

            // 4 parallel comb filters
            for (int c = 0; c < 4; ++c) {
                uint32_t& w   = wh[c];
                float filtered = comb[c][w];
                float store    = input + filtered * (roomSize * 0.9f);
                // One-pole LPF inside the comb (damping)
                store = filtered * (1.0f - damping) + store * damping;
                comb[c][w] = store;
                w = (w + 1) % COMB_LEN[c];
                out += filtered;
            }
            out *= 0.25f;  // mix down

            // 2 series all-pass filters
            for (int a = 0; a < 2; ++a) {
                uint32_t& w   = wh[4 + a];
                float buffered = ap[a][w];
                float store    = out + buffered * 0.5f;
                ap[a][w] = store;
                w = (w + 1) % AP_LEN[a];
                out = buffered - out;
            }

            buf.data[i] = buf.data[i] * (1.0f - roomSize * 0.5f) + out * roomSize * 0.5f;
        }
    }
};
