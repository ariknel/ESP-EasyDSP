#pragma once
#include <math.h>
#include "AudioTypes.h"

// =============================================================================
// CoeffCalc.h  –  Biquad coefficient calculator
// All formulas from "Audio EQ Cookbook" – R. Bristow-Johnson
// Input:  human values (Hz, Q, dB, sample-rate)
// Output: normalised BiquadCoeffs ready to pass to dsps_biquad_f32()
// =============================================================================

class CoeffCalc {
public:

    static BiquadCoeffs lowPass(float f, float Q, float fs) {
        float w0 = w(f, fs), cw = cosf(w0), sw = sinf(w0);
        float al = sw / (2.f * Q);
        return norm((1.f - cw) / 2.f,
                     1.f - cw,
                    (1.f - cw) / 2.f,
                     1.f + al, -2.f * cw, 1.f - al);
    }

    static BiquadCoeffs highPass(float f, float Q, float fs) {
        float w0 = w(f, fs), cw = cosf(w0), sw = sinf(w0);
        float al = sw / (2.f * Q);
        return norm( (1.f + cw) / 2.f,
                    -(1.f + cw),
                     (1.f + cw) / 2.f,
                      1.f + al, -2.f * cw, 1.f - al);
    }

    // bandwidth_hz → Q conversion: Q = f0 / BW
    static BiquadCoeffs bandPass(float f, float bwHz, float fs) {
        float Q  = (bwHz > 0.f) ? f / bwHz : 1.f;
        float w0 = w(f, fs), sw = sinf(w0), cw = cosf(w0);
        float al = sw / (2.f * Q);
        return norm(al, 0.f, -al,
                    1.f + al, -2.f * cw, 1.f - al);
    }

    static BiquadCoeffs notch(float f, float fs) {
        const float Q = 30.f;   // narrow surgical notch
        float w0 = w(f, fs), sw = sinf(w0), cw = cosf(w0);
        float al = sw / (2.f * Q);
        return norm(1.f, -2.f * cw, 1.f,
                    1.f + al, -2.f * cw, 1.f - al);
    }

    static BiquadCoeffs peakEQ(float f, float Q, float dBgain, float fs) {
        float A  = powf(10.f, dBgain / 40.f);
        float w0 = w(f, fs), sw = sinf(w0), cw = cosf(w0);
        float al = sw / (2.f * Q);
        return norm(1.f + al * A,  -2.f * cw,  1.f - al * A,
                    1.f + al / A,  -2.f * cw,  1.f - al / A);
    }

    static BiquadCoeffs lowShelf(float f, float dBgain, float fs) {
        float A  = powf(10.f, dBgain / 40.f);
        float w0 = w(f, fs), cw = cosf(w0), sw = sinf(w0);
        float al = sw / 2.f * sqrtf((A + 1.f / A) * (1.f / 1.f - 1.f) + 2.f);
        float b0 =  A * ((A+1) - (A-1)*cw + 2*sqrtf(A)*al);
        float b1 =  2 * A * ((A-1) - (A+1)*cw);
        float b2 =  A * ((A+1) - (A-1)*cw - 2*sqrtf(A)*al);
        float a0 =       (A+1) + (A-1)*cw + 2*sqrtf(A)*al;
        float a1 = -2 *  ((A-1) + (A+1)*cw);
        float a2 =        (A+1) + (A-1)*cw - 2*sqrtf(A)*al;
        return norm(b0, b1, b2, a0, a1, a2);
    }

    static BiquadCoeffs highShelf(float f, float dBgain, float fs) {
        float A  = powf(10.f, dBgain / 40.f);
        float w0 = w(f, fs), cw = cosf(w0), sw = sinf(w0);
        float al = sw / 2.f * sqrtf((A + 1.f / A) * (1.f / 1.f - 1.f) + 2.f);
        float b0 =  A * ((A+1) + (A-1)*cw + 2*sqrtf(A)*al);
        float b1 = -2 * A * ((A-1) + (A+1)*cw);
        float b2 =  A * ((A+1) + (A-1)*cw - 2*sqrtf(A)*al);
        float a0 =       (A+1) - (A-1)*cw + 2*sqrtf(A)*al;
        float a1 =  2 *  ((A-1) - (A+1)*cw);
        float a2 =        (A+1) - (A-1)*cw - 2*sqrtf(A)*al;
        return norm(b0, b1, b2, a0, a1, a2);
    }

private:
    static inline float w(float f, float fs) { return 2.f * M_PI * f / fs; }

    static BiquadCoeffs norm(float b0, float b1, float b2,
                              float a0, float a1, float a2) {
        BiquadCoeffs c;
        c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
        c.a1 = a1 / a0;  c.a2 = a2 / a0;
        return c;
    }
};
