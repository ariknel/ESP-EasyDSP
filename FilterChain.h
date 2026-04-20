#pragma once
#include <string.h>
#include "AudioTypes.h"
#include "../AudioConfig.h"

// esp-dsp — SIMD-accelerated biquad on ESP32 Xtensa LX6
// The _ae32 variant uses the DSP extension instructions.
#include "dsps_biquad.h"

// =============================================================================
// FilterChain.h  —  Holds up to MAX_BIQUAD_STAGES biquad stages and
//                   processes a stereo-interleaved float buffer through them.
//
// Buffer layout expected by process():
//   [L0, R0, L1, R1, L2, R2, ...]   (stereo interleaved, frames * 2 floats)
//
// dsps_biquad_f32_ae32() signature:
//   esp_err_t dsps_biquad_f32_ae32(const float *input,
//                                   float       *output,
//                                   int          len,
//                                   float       *coef,   // float[5]
//                                   float       *w);     // float[2] state
//
// Coefficient order expected by esp-dsp:
//   coef[0] = b0,  coef[1] = b1,  coef[2] = b2
//   coef[3] = a1,  coef[4] = a2
//
// SIGN CONVENTION: esp-dsp uses the Direct Form II Transposed difference
// equation where a1 and a2 are ADDED (not subtracted).  The Audio EQ Cookbook
// defines them with a negative sign.  CoeffCalc already normalises by a0, so
// we must negate a1 and a2 when building the coef[] array for esp-dsp.
//
// If all your filters sound "backwards" (LP acts like HP, boost acts like cut),
// remove the negation on lines marked NEGATE.
// =============================================================================

class FilterChain {
public:

    // -------------------------------------------------------------------------
    // Add a biquad stage to the end of the chain.
    // Returns the stage index (0-based) for later updates, or -1 if full.
    // -------------------------------------------------------------------------
    int addStage(const BiquadCoeffs& c) {
        if (_count >= MAX_BIQUAD_STAGES) return -1;
        int idx = _count++;
        _stages[idx].c      = c;
        _stages[idx].active = true;
        _stages[idx].stL    = {};
        _stages[idx].stR    = {};
        return idx;
    }

    // -------------------------------------------------------------------------
    // Replace coefficients of an existing stage without resetting state.
    // This is the "no-click live update" path.
    // -------------------------------------------------------------------------
    void updateStage(int idx, const BiquadCoeffs& c) {
        if (idx < 0 || idx >= _count) return;
        _stages[idx].c = c;
        // Deliberately do NOT clear stL/stR — avoids audible click on update
    }

    // -------------------------------------------------------------------------
    // Remove all stages.
    // -------------------------------------------------------------------------
    void clear() {
        memset(_stages, 0, sizeof(_stages));
        _count = 0;
    }

    int  stageCount() const { return _count; }
    bool empty()      const { return _count == 0; }

    // -------------------------------------------------------------------------
    // Process a stereo-interleaved buffer in-place.
    //
    // @param buf     Pointer to float array: [L0,R0,L1,R1,...], length=frames*2
    // @param frames  Number of stereo frames (half the float array length)
    // -------------------------------------------------------------------------
    void process(float* buf, int frames) {
        if (_count == 0 || frames == 0) return;

        for (int s = 0; s < _count; ++s) {
            if (!_stages[s].active) continue;

            BiquadStage& st = _stages[s];

            // Build esp-dsp coefficient array.
            // CoeffCalc produces Cookbook-sign a1/a2; esp-dsp wants negated.
            float coef[5] = {
                 st.c.b0,
                 st.c.b1,
                 st.c.b2,
                -st.c.a1,   // NEGATE — remove minus if filters sound inverted
                -st.c.a2    // NEGATE — remove minus if filters sound inverted
            };

            // esp-dsp DF2T state: two floats per channel
            float wL[2] = { st.stL.y1, st.stL.y2 };
            float wR[2] = { st.stR.y1, st.stR.y2 };

            // Left channel  — start at buf[0], step 2 (skip R samples)
            dsps_biquad_f32_ae32(buf,     buf,     frames, coef, wL);
            // Right channel — start at buf[1], step 2 (skip L samples)
            dsps_biquad_f32_ae32(buf + 1, buf + 1, frames, coef, wR);

            // Save state back
            st.stL.y1 = wL[0]; st.stL.y2 = wL[1];
            st.stR.y1 = wR[0]; st.stR.y2 = wR[1];
        }
    }

private:
    BiquadStage _stages[MAX_BIQUAD_STAGES] = {};
    int         _count = 0;
};
