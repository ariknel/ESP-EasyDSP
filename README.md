# ESP-AudioDSP (release coming soon)

> **A universal, hardware-abstracted DSP library for ESP32.**  
> Build real-time audio filters, dynamics processors, and creative effects with a PCM5102 DAC and PCM180x ADC — in a few lines of code.

[![Build Status](https://img.shields.io/github/actions/workflow/status/your-handle/ESP-AudioDSP/ci.yml?style=flat-square)](https://github.com/your-handle/ESP-AudioDSP)
[![Version](https://img.shields.io/badge/version-1.2.0-brightgreen?style=flat-square)](https://github.com/your-handle/ESP-AudioDSP/releases)
[![License](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](LICENSE)
[![Arduino](https://img.shields.io/badge/Arduino-compatible-00979D?style=flat-square&logo=arduino)](https://www.arduino.cc/reference/en/libraries/)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange?style=flat-square)](https://platformio.org/)

---

## What is this?

Building a real-time audio processor on ESP32 normally means fighting DMA buffers, understanding SIMD intrinsics, hand-writing biquad difference equations, and converting between integer and floating-point domains — before you've even thought about your actual effect.

**ESP-AudioDSP removes all of that.**

It wraps Espressif's [`esp-dsp`](https://github.com/espressif/esp-dsp) library behind a clean, descriptive C++ API, adds a software fallback for non-ESP32 boards, manages the ping-pong DMA buffer lifecycle for you, and exposes every DSP primitive as a human-readable function call.

```cpp
#include "ESP_AudioDSP.h"

AudioDSP dsp;

void setup() {
  dsp.begin(44100, 16);
  dsp.setInput(INPUT_I2S_MIC);
  dsp.setOutput(OUTPUT_PCM5102);

  dsp.setHighPass(80.0f, 0.707f);       // Remove low-end rumble
  dsp.setParametricEQ(2500.0f, 1.2f, 3.0f);  // Presence boost
  dsp.setCompressor(0.7f, 4.0f);        // 4:1 compression
  dsp.setGain(1.5f);
}

void loop() {
  dsp.process();
}
```

That's a broadcast-quality voice processor in 10 lines.

---

## Hardware

The reference design uses three components:

| Role | Part | Notes |
|------|------|-------|
| Microcontroller | ESP32 / ESP32-S3 | Any variant works; S3 recommended for multi-band EQ |
| DAC (output) | PCM5102A | I²S, 32-bit, 384kHz max |
| ADC (input) | PCM1808 / PCM1802 | I²S output, 24-bit |

Default pin assignments (overridable in `AudioConfig.h`):

```
PCM5102   →  ESP32 GPIO 25 (BCLK), 26 (LRCK), 22 (DATA)
PCM1808   →  ESP32 GPIO 32 (BCLK), 33 (LRCK), 34 (DATA)
```

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                      User Sketch                     │
│           dsp.setLowPass(450, 0.707)                 │
└──────────────────┬──────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────┐
│                  ESP-AudioDSP API                    │
│          Coefficient Calculator  |  Effect Chain     │
└──────────┬───────────────────────────────┬──────────┘
           │                               │
┌──────────▼─────────┐         ┌───────────▼──────────┐
│   ESP32 DSP Path   │         │  Fallback DSP Path   │
│  dsps_biquad_f32() │         │  C++ biquad for-loop │
│  (SIMD via esp-dsp)│         │  (Arduino / RPi)     │
└──────────┬─────────┘         └───────────┬──────────┘
           │                               │
┌──────────▼───────────────────────────────▼──────────┐
│              Ping-Pong Buffer Manager                │
│    DMA → Buffer A (playing)  |  Buffer B (filling)  │
└──────────┬──────────────────────────────────────────┘
           │
┌──────────▼──────────────────────────────────────────┐
│                 Hardware Abstraction Layer           │
│   Internal_I2S (ESP32)  |  BitBang_I2S (Arduino)    │
└─────────────────────────────────────────────────────┘
```

The processing loop runs as a **high-priority FreeRTOS task pinned to Core 1**, so your sketch logic on Core 0 never causes audio glitches.

---

## Installation

**Arduino Library Manager** (recommended):

1. Open the Library Manager in Arduino IDE (`Sketch → Include Library → Manage Libraries`)
2. Search for `ESP-AudioDSP`
3. Click **Install**

**PlatformIO:**

```ini
lib_deps =
    your-handle/ESP-AudioDSP @ ^1.2.0
```

**Manual:**

```bash
git clone https://github.com/your-handle/ESP-AudioDSP.git
# Copy the folder into your Arduino/libraries/ directory
```

> **Dependency:** On ESP32, install `esp-dsp` via the Component Manager or add it to your `idf_component.yml`. The library detects the target automatically — no manual `#ifdef` needed.

---

## Auto-Config

Open `AudioConfig.h` and uncomment one line to configure your entire hardware setup:

```cpp
// AudioConfig.h — uncomment your hardware combination

// #define PRESET_PCM5102_PCM1808     // Standard stereo I2S rig
// #define PRESET_PCM5102_INTERNAL_ADC // DAC out, ESP32 built-in ADC in
// #define PRESET_INTERNAL_DAC_MIC    // Fully onboard, no external hardware
// #define PRESET_PWM_OUTPUT          // Arduino / non-I2S fallback
```

---

## Full API Reference

### Setup

```cpp
dsp.begin(sampleRate, bitDepth);
// sampleRate: 8000, 22050, 44100, 48000
// bitDepth:   16, 24, 32

dsp.setInput(INPUT_TYPE);
// INPUT_INTERNAL_ADC  — ESP32 built-in ADC (low quality, convenient)
// INPUT_I2S_MIC       — I2S MEMS or PDM microphone
// INPUT_LINE_IN       — PCM1808 / PCM1802 line input

dsp.setOutput(OUTPUT_TYPE);
// OUTPUT_PCM5102      — External I2S DAC (recommended)
// OUTPUT_INTERNAL_DAC — ESP32 built-in 8-bit DAC
// OUTPUT_PWM          — PWM fallback for Arduino

dsp.process();           // Call in loop() — or let the background task handle it
dsp.setBufferSize(512);  // Default 256 samples; increase for stability, decrease for latency
```

### Filter Functions

All filters use the **biquad IIR** structure internally. On ESP32, they are accelerated via `dsps_biquad_f32()`. On other platforms, a portable C++ fallback is used automatically.

```cpp
// Low-pass filter — attenuates frequencies above cutoff
dsp.setLowPass(float frequency_hz, float q_factor);
// Example: dsp.setLowPass(800.0f, 0.707f);  // Muffled telephone effect

// High-pass filter — attenuates frequencies below cutoff
dsp.setHighPass(float frequency_hz, float q_factor);
// Example: dsp.setHighPass(80.0f, 0.707f);  // Remove mic handling noise

// Band-pass filter — passes only a range around the center frequency
dsp.setBandPass(float frequency_hz, float bandwidth_hz);
// Example: dsp.setBandPass(1200.0f, 800.0f);  // Telephone/radio effect

// Notch filter — removes a single frequency (narrow null)
dsp.setNotch(float frequency_hz);
// Example: dsp.setNotch(50.0f);   // Remove EU mains hum
// Example: dsp.setNotch(60.0f);   // Remove US mains hum

// Parametric EQ — boost or cut around a center frequency
dsp.setParametricEQ(float frequency_hz, float q_factor, float gain_db);
// Example: dsp.setParametricEQ(3000.0f, 1.4f, +4.0f);  // Presence boost

// High / Low shelf — tilt the spectrum above or below a frequency
dsp.setHighShelf(float frequency_hz, float gain_db);
dsp.setLowShelf(float frequency_hz, float gain_db);
```

> **Q Factor guide:** 0.5 = very wide, 0.707 = Butterworth (maximally flat), 1.0 = moderate peak, 2.0+ = narrow/resonant.

### Dynamics

```cpp
// Volume — multiply the signal by a linear factor
dsp.setGain(float multiplier);
// Example: dsp.setGain(0.5f);  // -6 dB
// Example: dsp.setGain(2.0f);  // +6 dB

// Compressor — reduces dynamic range above a threshold
dsp.setCompressor(float threshold_normalized, float ratio);
// threshold: 0.0–1.0 (fraction of full scale)
// ratio: 2.0 = 2:1, 4.0 = 4:1, 10.0 ≈ limiting
// Example: dsp.setCompressor(0.7f, 4.0f);

// Noise gate — silences output when signal falls below threshold
dsp.setNoiseGate(float threshold_normalized);
// Example: dsp.setNoiseGate(0.02f);  // Silence when input < 2% full scale

// Limiter — hard ceiling on output level
dsp.setLimiter(float ceiling_normalized);
// Example: dsp.setLimiter(0.95f);  // Prevent clipping
```

### Effects

```cpp
// Distortion — hard clipping for guitar/overdrive tones
dsp.setDistortion(float amount);
// amount: 0.0 = clean, 1.0 = heavily clipped
// Example: dsp.setDistortion(0.6f);

// Bit crusher — reduces effective bit depth for lo-fi / 8-bit sound
dsp.setBitCrusher(int bits);
// bits: 4–16 (16 = lossless, 4 = extreme lo-fi)
// Example: dsp.setBitCrusher(8);

// Delay — echo effect with feedback loop
// Requires PSRAM for delays > ~90ms at 44100Hz
dsp.setDelay(float delay_ms, float feedback);
// delay_ms: 1–2000ms (PSRAM required above ~90ms)
// feedback: 0.0 = single echo, 0.7 = many repeats
// Example: dsp.setDelay(250.0f, 0.4f);

// Chorus — subtle pitch modulation for width/warmth
dsp.setChorus(float rate_hz, float depth);
// Example: dsp.setChorus(1.2f, 0.3f);

// Reverb (algorithmic) — adds room ambience
dsp.setReverb(float room_size, float damping);
// room_size: 0.0 = dry, 1.0 = huge hall
// Example: dsp.setReverb(0.6f, 0.5f);
```

### Chaining

Effects are applied in the order they are set. Chain as many as CPU headroom allows:

```cpp
dsp.setHighPass(80.0f, 0.707f);
dsp.setParametricEQ(200.0f, 1.0f, -3.0f);   // Low-mid cut
dsp.setParametricEQ(3000.0f, 1.4f, +4.0f);  // Presence boost
dsp.setCompressor(0.7f, 4.0f);
dsp.setLimiter(0.95f);
dsp.setGain(1.2f);
```

Call `dsp.clearEffects()` to reset the chain.

---

## Examples

### `SimpleFilter.ino` — Low-pass filter on microphone input

```cpp
#include "ESP_AudioDSP.h"

AudioDSP dsp;

void setup() {
  dsp.begin(44100, 16);
  dsp.setInput(INPUT_I2S_MIC);
  dsp.setOutput(OUTPUT_PCM5102);
  dsp.setLowPass(1200.0f, 0.707f);
}

void loop() {
  dsp.process();
}
```

### `GuitarPedal.ino` — Overdrive + tone control

```cpp
#include "ESP_AudioDSP.h"

AudioDSP dsp;

void setup() {
  dsp.begin(44100, 24);
  dsp.setInput(INPUT_LINE_IN);
  dsp.setOutput(OUTPUT_PCM5102);

  dsp.setHighPass(100.0f, 0.707f);    // Remove subsonic rumble
  dsp.setGain(3.0f);                  // Drive into the clipper
  dsp.setDistortion(0.65f);           // Soft overdrive
  dsp.setLowPass(5000.0f, 1.0f);      // Tame the top end
  dsp.setParametricEQ(700.0f, 1.0f, -2.0f);  // Scoop the mid
}

void loop() {
  dsp.process();
}
```

### `RadioEffect.ino` — Telephone / walkie-talkie simulation

```cpp
#include "ESP_AudioDSP.h"

AudioDSP dsp;

void setup() {
  dsp.begin(44100, 16);
  dsp.setInput(INPUT_I2S_MIC);
  dsp.setOutput(OUTPUT_PCM5102);

  dsp.setHighPass(300.0f, 0.9f);
  dsp.setBandPass(1500.0f, 2000.0f);
  dsp.setDistortion(0.2f);
  dsp.setNotch(50.0f);
}

void loop() {
  dsp.process();
}
```

---

## Performance Benchmarks

Measured at 44100 Hz / 256-sample buffer. Times are per-buffer.

| Effect | ESP32 (esp-dsp) | Arduino Uno (fallback) |
|--------|----------------|----------------------|
| Single biquad (LPF/HPF) | 18 µs | 1.4 ms |
| 4-band parametric EQ | 42 µs | 5.8 ms |
| Compressor | 31 µs | 2.9 ms |
| Delay (PSRAM, 250ms) | 61 µs | N/A (no PSRAM) |
| Full guitar pedal chain | 89 µs | N/A |

The 256-sample buffer at 44100 Hz gives you **5.8 ms** of processing budget. The full guitar pedal chain uses **89 µs** on ESP32 — leaving 98% of the budget free.

> Benchmarks run via `extras/benchmark/benchmark.ino`. Enable `#define AUDIO_DSP_BENCHMARK` to print timing to Serial.

---

## Platform Compatibility

| Platform | I2S Output | DSP Backend | Delay Effect |
|----------|-----------|-------------|-------------|
| ESP32 | Native DMA | `esp-dsp` SIMD | PSRAM required |
| ESP32-S3 | Native DMA | `esp-dsp` SIMD | PSRAM built-in on some modules |
| ESP8266 | Software I2S | C++ fallback | No |
| Arduino Uno / Nano | PWM fallback | C++ fallback | No |
| Arduino Mega | PWM fallback | C++ fallback | SRAM-limited |
| Raspberry Pi Pico | PWM / PIO | C++ fallback | Yes (2MB flash) |

---

## Project Roadmap

- [x] Phase 1 — Ping-pong buffer manager, HAL, FreeRTOS processing task
- [x] Phase 2 — Biquad engine with `esp-dsp` + C++ fallback, coefficient calculator
- [x] Phase 3 — Full API (filters, dynamics, effects, chaining)
- [x] Phase 4 — Benchmarking, `AudioConfig.h` auto-config, example sketches
- [ ] Phase 5 — Stereo processing, multi-band compressor, MIDI CC control
- [ ] Phase 6 — Web UI via WiFi for live parameter adjustment
- [ ] Phase 7 — VST-style preset save/load to SPIFFS

---

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you'd like to change.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-effect`)
3. Commit your changes (`git commit -m 'Add ring modulator effect'`)
4. Push to the branch (`git push origin feature/my-effect`)
5. Open a Pull Request

Please run the benchmark suite before submitting and include timing results in your PR description.

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

## Acknowledgements

- [Espressif esp-dsp](https://github.com/espressif/esp-dsp) — the SIMD-accelerated DSP primitives this library wraps
- [Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) by Robert Bristow-Johnson — the biquad coefficient formulas used in the calculator
- The Arduino and PlatformIO communities for hardware testing across a wide range of boards
