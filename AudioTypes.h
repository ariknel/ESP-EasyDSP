#pragma once
#include <stdint.h>

// =============================================================================
// AudioTypes.h — Shared enums, structs, and constants
// =============================================================================

// --- Input sources ---
enum InputType {
    INPUT_INTERNAL_ADC = 0,
    INPUT_I2S_MIC,
    INPUT_LINE_IN
};

// --- Output destinations ---
enum OutputType {
    OUTPUT_PCM5102 = 0,
    OUTPUT_INTERNAL_DAC
};

// --- Effect types (used internally by the effect chain) ---
enum EffectType {
    EFFECT_NONE = 0,
    EFFECT_LOWPASS,
    EFFECT_HIGHPASS,
    EFFECT_BANDPASS,
    EFFECT_NOTCH,
    EFFECT_PARAMETRIC_EQ,
    EFFECT_LOW_SHELF,
    EFFECT_HIGH_SHELF,
    EFFECT_GAIN,
    EFFECT_COMPRESSOR,
    EFFECT_NOISE_GATE,
    EFFECT_LIMITER,
    EFFECT_DISTORTION,
    EFFECT_BITCRUSHER,
    EFFECT_DELAY,
    EFFECT_CHORUS,
    EFFECT_REVERB
};

// --- Max number of effects in the chain ---
#define AUDIO_MAX_EFFECTS 16

// --- Biquad coefficient set (Direct Form I) ---
struct BiquadCoeffs {
    float b0, b1, b2;  // feed-forward
    float a1, a2;       // feed-back (a0 normalised to 1.0)
};

// --- Biquad state (two delay elements per channel) ---
struct BiquadState {
    float x1 = 0.0f, x2 = 0.0f;  // input history
    float y1 = 0.0f, y2 = 0.0f;  // output history
};

// --- Generic effect descriptor stored in the chain ---
struct EffectSlot {
    EffectType  type   = EFFECT_NONE;
    bool        active = false;

    // Biquad filters store their coefficients here
    BiquadCoeffs coeffs = {};
    BiquadState  state  = {};

    // Dynamics / simple effects store scalar params here
    float param0 = 0.0f;   // threshold / gain / amount / room_size / rate_hz / delay_ms / bits
    float param1 = 0.0f;   // ratio / feedback / depth / damping
    float param2 = 0.0f;   // (reserved)

    // Delay effect: pointer into a heap-allocated ring buffer
    float*   delayBuf   = nullptr;
    uint32_t delayLen   = 0;
    uint32_t delayWrite = 0;
};

// --- Audio format carried through the processing pipeline ---
struct AudioBuffer {
    float*   data;     // interleaved float samples, normalised -1.0 … +1.0
    uint32_t frames;   // number of frames (samples per channel)
    uint8_t  channels; // 1 = mono, 2 = stereo
};
