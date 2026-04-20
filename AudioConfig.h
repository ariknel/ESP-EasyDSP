#pragma once

// =============================================================================
// AudioConfig.h  –  Hardware pin assignments and compile-time constants
// Edit this file to match your wiring. Everything else is automatic.
// =============================================================================

// --- PCM5102A  (I2S output DAC) ---
#define PIN_I2S_OUT_BCK   26   // Bit clock
#define PIN_I2S_OUT_WS    25   // Word select / LRCK
#define PIN_I2S_OUT_DATA  22   // Serial data

// --- PCM1808 / line-in ADC  (I2S input) ---
// If you are using the ESP32 internal ADC instead, leave these defined
// but set INPUT_SOURCE to INPUT_INTERNAL_ADC in your sketch.
#define PIN_I2S_IN_BCK    32
#define PIN_I2S_IN_WS     33
#define PIN_I2S_IN_DATA   34

// --- Internal ADC pin (used when INPUT_INTERNAL_ADC is selected) ---
#define PIN_ADC_IN        36   // GPIO36 = VP, ADC1_CH0  (input-only, no pull)

// --- DMA buffer size in samples (per channel) ---
// 256 @ 44100 Hz  →  ~5.8 ms latency  (good starting point)
// Increase to 512 if you hear glitches; decrease to 128 for lower latency.
#define DMA_BUF_LEN    256
#define DMA_BUF_COUNT  4      // number of DMA descriptors in the ring

// --- FreeRTOS task settings ---
#define DSP_TASK_CORE      1   // pin DSP to Core 1, leaving Core 0 for Arduino loop
#define DSP_TASK_PRIORITY  22  // high priority; lower than WiFi (23) if used
#define DSP_TASK_STACK     4096

// --- Maximum biquad stages in the filter chain ---
#define MAX_BIQUAD_STAGES  8
