#pragma once
#include <math.h>
#include "AudioTypes.h"

// =============================================================================
// CoeffCalc.h — Biquad coefficient calculator
// All formulas from "Audio EQ Cookbook" by Robert Bristow-Johnson.
// Inputs are human values (Hz, Q, dB); outputs are the 5 biquad constants.
// =============================================================================

class CoeffCalc {
public:

    // -------------------------------------------------------------------------
    // Low-pass filter
    // -------------------------------------------------------------------------
    static BiquadCoeffs lowPass(float freq, float q, float fs) {
        float w0    = 2.0f * M_PI * freq / fs;
        float cosW0 = cosf(w0);
        float alpha = sinf(w0) / (2.0f * q);
        float b0    = (1.0f - cosW0) / 2.0f;
        float b1    =  1.0f - cosW0;
        float b2    = (1.0f - cosW0) / 2.0f;
        float a0    =  1.0f + alpha;
        float a1    = -2.0f * cosW0;
        float a2    =  1.0f - alpha;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

    // -------------------------------------------------------------------------
    // High-pass filter
    // -------------------------------------------------------------------------
    static BiquadCoeffs highPass(float freq, float q, float fs) {
        float w0    = 2.0f * M_PI * freq / fs;
        float cosW0 = cosf(w0);
        float alpha = sinf(w0) / (2.0f * q);
        float b0    =  (1.0f + cosW0) / 2.0f;
        float b1    = -(1.0f + cosW0);
        float b2    =  (1.0f + cosW0) / 2.0f;
        float a0    =  1.0f + alpha;
        float a1    = -2.0f * cosW0;
        float a2    =  1.0f - alpha;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

    // -------------------------------------------------------------------------
    // Band-pass filter (constant skirt, peak gain = Q)
    // bandwidth_hz is converted to Q internally
    // -------------------------------------------------------------------------
    static BiquadCoeffs bandPass(float freq, float bandwidth_hz, float fs) {
        float q     = freq / bandwidth_hz;
        float w0    = 2.0f * M_PI * freq / fs;
        float alpha = sinf(w0) / (2.0f * q);
        float b0    =  alpha;
        float b1    =  0.0f;
        float b2    = -alpha;
        float a0    =  1.0f + alpha;
        float a1    = -2.0f * cosf(w0);
        float a2    =  1.0f - alpha;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

    // -------------------------------------------------------------------------
    // Notch filter
    // -------------------------------------------------------------------------
    static BiquadCoeffs notch(float freq, float fs) {
        const float Q   = 30.0f;  // very narrow — surgical notch
        float w0        = 2.0f * M_PI * freq / fs;
        float alpha     = sinf(w0) / (2.0f * Q);
        float cosW0     = cosf(w0);
        float b0        =  1.0f;
        float b1        = -2.0f * cosW0;
        float b2        =  1.0f;
        float a0        =  1.0f + alpha;
        float a1        = -2.0f * cosW0;
        float a2        =  1.0f - alpha;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

    // -------------------------------------------------------------------------
    // Peaking EQ (parametric)
    // -------------------------------------------------------------------------
    static BiquadCoeffs parametricEQ(float freq, float q, float gain_db, float fs) {
        float A     = powf(10.0f, gain_db / 40.0f);
        float w0    = 2.0f * M_PI * freq / fs;
        float alpha = sinf(w0) / (2.0f * q);
        float cosW0 = cosf(w0);
        float b0    =  1.0f + alpha * A;
        float b1    = -2.0f * cosW0;
        float b2    =  1.0f - alpha * A;
        float a0    =  1.0f + alpha / A;
        float a1    = -2.0f * cosW0;
        float a2    =  1.0f - alpha / A;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

    // -------------------------------------------------------------------------
    // Low shelf
    // -------------------------------------------------------------------------
    static BiquadCoeffs lowShelf(float freq, float gain_db, float fs) {
        float A     = powf(10.0f, gain_db / 40.0f);
        float w0    = 2.0f * M_PI * freq / fs;
        float cosW0 = cosf(w0);
        float sinW0 = sinf(w0);
        float alpha = sinW0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / 1.0f - 1.0f) + 2.0f);
        float b0    =  A * ((A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqrtf(A) * alpha);
        float b1    =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW0);
        float b2    =  A * ((A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqrtf(A) * alpha);
        float a0    =  (A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqrtf(A) * alpha;
        float a1    = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW0);
        float a2    =  (A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqrtf(A) * alpha;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

    // -------------------------------------------------------------------------
    // High shelf
    // -------------------------------------------------------------------------
    static BiquadCoeffs highShelf(float freq, float gain_db, float fs) {
        float A     = powf(10.0f, gain_db / 40.0f);
        float w0    = 2.0f * M_PI * freq / fs;
        float cosW0 = cosf(w0);
        float sinW0 = sinf(w0);
        float alpha = sinW0 / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / 1.0f - 1.0f) + 2.0f);
        float b0    =  A * ((A + 1.0f) + (A - 1.0f) * cosW0 + 2.0f * sqrtf(A) * alpha);
        float b1    = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW0);
        float b2    =  A * ((A + 1.0f) + (A - 1.0f) * cosW0 - 2.0f * sqrtf(A) * alpha);
        float a0    =  (A + 1.0f) - (A - 1.0f) * cosW0 + 2.0f * sqrtf(A) * alpha;
        float a1    =  2.0f * ((A - 1.0f) - (A + 1.0f) * cosW0);
        float a2    =  (A + 1.0f) - (A - 1.0f) * cosW0 - 2.0f * sqrtf(A) * alpha;
        return normalise(b0, b1, b2, a0, a1, a2);
    }

private:
    // Normalise all coefficients by a0 so the difference equation needs no division
    static BiquadCoeffs normalise(float b0, float b1, float b2,
                                   float a0, float a1, float a2) {
        BiquadCoeffs c;
        c.b0 = b0 / a0;
        c.b1 = b1 / a0;
        c.b2 = b2 / a0;
        c.a1 = a1 / a0;
        c.a2 = a2 / a0;
        return c;
    }
};
