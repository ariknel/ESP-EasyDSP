#pragma once

// =============================================================================
// AudioConfig.h — ESP-AudioDSP Hardware Configuration
// =============================================================================
// Uncomment ONE preset below, or define pins manually underneath.
// This file is the only place you need to touch for hardware setup.
// =============================================================================

// --- PRESETS — uncomment one ---

// #define PRESET_PCM5102_PCM1808       // PCM5102A DAC + PCM1808 ADC (recommended)
// #define PRESET_PCM5102_INTERNAL_ADC  // PCM5102A DAC + ESP32 internal ADC
// #define PRESET_INTERNAL_DAC_MIC      // ESP32 built-in DAC + I2S MEMS mic

// --- Apply preset pin defaults ---

#if defined(PRESET_PCM5102_PCM1808)
  #define AUDIO_OUTPUT  OUTPUT_PCM5102
  #define AUDIO_INPUT   INPUT_LINE_IN
  #define I2S_OUT_BCLK  25
  #define I2S_OUT_LRCK  26
  #define I2S_OUT_DATA  22
  #define I2S_IN_BCLK   32
  #define I2S_IN_LRCK   33
  #define I2S_IN_DATA   34

#elif defined(PRESET_PCM5102_INTERNAL_ADC)
  #define AUDIO_OUTPUT  OUTPUT_PCM5102
  #define AUDIO_INPUT   INPUT_INTERNAL_ADC
  #define I2S_OUT_BCLK  25
  #define I2S_OUT_LRCK  26
  #define I2S_OUT_DATA  22
  #define ADC_PIN       36  // VP pin

#elif defined(PRESET_INTERNAL_DAC_MIC)
  #define AUDIO_OUTPUT  OUTPUT_INTERNAL_DAC
  #define AUDIO_INPUT   INPUT_I2S_MIC
  #define I2S_IN_BCLK   32
  #define I2S_IN_LRCK   33
  #define I2S_IN_DATA   34

#else
  // --- Manual pin configuration ---
  // No preset selected — define your own pins here:
  #define I2S_OUT_BCLK  25
  #define I2S_OUT_LRCK  26
  #define I2S_OUT_DATA  22
  #define I2S_IN_BCLK   32
  #define I2S_IN_LRCK   33
  #define I2S_IN_DATA   34
#endif

// --- Optional: PSRAM-backed delay ---
// Define this if your ESP32 module has PSRAM (e.g. ESP32-WROVER, ESP32-S3)
// Without this, delay is limited to ~90ms at 44100Hz
// #define BOARD_HAS_PSRAM

// --- Optional: print timing to Serial ---
// #define AUDIO_DSP_BENCHMARK

// --- Optional: manual process() instead of background task ---
// #define AUDIO_DSP_MANUAL_PROCESS

// --- Buffer defaults (override if needed) ---
#ifndef AUDIO_BUFFER_SIZE
  #define AUDIO_BUFFER_SIZE 256
#endif

#ifndef AUDIO_TASK_CORE
  #define AUDIO_TASK_CORE 1
#endif

#ifndef AUDIO_TASK_PRIORITY
  #define AUDIO_TASK_PRIORITY 22
#endif
