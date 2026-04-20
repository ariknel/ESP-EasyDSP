#pragma once
#include <stdint.h>

// =============================================================================
// AudioTypes.h  –  Shared enums, structs, constants
// =============================================================================

// Input source selection
enum InputSource : uint8_t {
    INPUT_I2S_ADC      = 0,   // External I2S ADC (e.g. PCM1808)
    INPUT_INTERNAL_ADC = 1,   // ESP32 built-in ADC (lower quality, no ext. parts)
};

// Output destination
enum OutputDest : uint8_t {
    OUTPUT_PCM5102     = 0,   // External I2S DAC (PCM5102A) – recommended
    OUTPUT_INTERNAL_DAC= 1,   // ESP32 built-in 8-bit DAC on GPIO 25/26
};

// Biquad filter type – used internally by CoeffCalc
enum FilterType : uint8_t {
    FILTER_LOWPASS    = 0,
    FILTER_HIGHPASS,
    FILTER_BANDPASS,
    FILTER_NOTCH,
    FILTER_PEAK_EQ,
    FILTER_LOW_SHELF,
    FILTER_HIGH_SHELF,
};

// One set of biquad coefficients (Direct Form I, normalised by a0)
struct BiquadCoeffs {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;   // feed-forward
    float a1 = 0.f, a2 = 0.f;               // feed-back  (a0 absorbed → 1)
};

// Per-channel filter state (two delay elements)
struct BiquadState {
    float x1 = 0.f, x2 = 0.f;
    float y1 = 0.f, y2 = 0.f;
};

// One stage in the filter chain (one biquad, two states: L + R)
struct BiquadStage {
    bool        active = false;
    BiquadCoeffs c;
    BiquadState  stL;   // left channel state
    BiquadState  stR;   // right channel state
};
