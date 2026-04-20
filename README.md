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

**note** this is a vibecoded library, a personal project - but it works! But there are most likely still tons of bugs that remain unresolved, if so let me know what you run into and i will try my best fo fix asap

**ESP-AudioDSP removes all of that.**

It wraps Espressif's [`esp-dsp`](https://github.com/espressif/esp-dsp) library behind a clean, descriptive C++ API, manages the ping-pong DMA buffer lifecycle for you, and exposes every DSP primitive as a human-readable function call.

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

> All functions are called on an `AudioDSP` object (conventionally named `dsp`). Effects are added to a chain and processed in the order they were set. Call `dsp.clearEffects()` to reset the chain at any time.

---

### Table of Contents

| Category | Functions |
|----------|-----------|
| [Setup](#setup-functions) | `begin` · `setInput` · `setOutput` · `setBufferSize` · `process` · `clearEffects` |
| [Filters](#filter-functions) | `setLowPass` · `setHighPass` · `setBandPass` · `setNotch` · `setParametricEQ` · `setLowShelf` · `setHighShelf` |
| [Dynamics](#dynamics-functions) | `setGain` · `setCompressor` · `setNoiseGate` · `setLimiter` |
| [Effects](#effect-functions) | `setDistortion` · `setBitCrusher` · `setDelay` · `setChorus` · `setReverb` |

---

## Setup Functions

---

### `begin()`

Initializes the audio engine, configures the I2S peripheral, and starts the background processing task.

```cpp
dsp.begin(uint32_t sampleRate, uint8_t bitDepth);
```

| Parameter | Type | Valid Values | Description |
|-----------|------|-------------|-------------|
| `sampleRate` | `uint32_t` | `8000`, `22050`, `44100`, `48000` | Audio sample rate in Hz |
| `bitDepth` | `uint8_t` | `16`, `24`, `32` | Bit depth per sample |

**Must be called first**, before any other function. Calling `begin()` again with different parameters reinitializes the hardware.

```cpp
// Standard CD-quality audio
dsp.begin(44100, 16);

// High-quality audio (recommended for music)
dsp.begin(48000, 24);

// Low-bandwidth voice (saves processing headroom)
dsp.begin(16000, 16);
```

> **Note:** Higher sample rates increase CPU load. At 48000 Hz with a 256-sample buffer, the processing task runs every 5.3 ms. At 8000 Hz it runs every 32 ms, giving you far more headroom on slower boards.

---

### `setInput()`

Selects the audio input source.

```cpp
dsp.setInput(INPUT_TYPE type);
```

| Constant | Hardware | Quality | Notes |
|----------|----------|---------|-------|
| `INPUT_INTERNAL_ADC` | ESP32 built-in ADC | Low (8-bit effective) | No external parts needed; only use for prototyping |
| `INPUT_I2S_MIC` | I²S MEMS microphone | Good | Works with INMP441, SPH0645, ICS-43434 |
| `INPUT_LINE_IN` | PCM1808 / PCM1802 ADC | Excellent | For instruments, mixers, and line-level sources |

```cpp
dsp.setInput(INPUT_LINE_IN);  // Recommended for PCM180x ADC
```

> **Pin defaults** for `INPUT_LINE_IN`: BCLK → GPIO 32, LRCK → GPIO 33, DATA → GPIO 34. Override in `AudioConfig.h`.

---

### `setOutput()`

Selects the audio output destination.

```cpp
dsp.setOutput(OUTPUT_TYPE type);
```

| Constant | Hardware | Quality | Notes |
|----------|----------|---------|-------|
| `OUTPUT_PCM5102` | PCM5102A external DAC | Excellent | Recommended; 32-bit, 112dB SNR |
| `OUTPUT_INTERNAL_DAC` | ESP32 built-in DAC | Low (8-bit) | GPIO 25/26; no external parts needed |
| `OUTPUT_PWM` | Software PWM | Low | Fallback for Arduino boards without I²S |

```cpp
dsp.setOutput(OUTPUT_PCM5102);  // Recommended
```

> **Pin defaults** for `OUTPUT_PCM5102`: BCLK → GPIO 25, LRCK → GPIO 26, DATA → GPIO 22. Override in `AudioConfig.h`.

---

### `setBufferSize()`

Sets the DMA buffer size in samples. This controls the trade-off between latency and stability.

```cpp
dsp.setBufferSize(uint16_t samples);
```

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `samples` | `uint16_t` | `64`–`2048` | `256` | Number of samples per DMA buffer |

```cpp
dsp.setBufferSize(128);  // Lower latency (~3ms at 44100Hz) — may glitch on heavy chains
dsp.setBufferSize(256);  // Default — good balance
dsp.setBufferSize(512);  // Higher latency (~12ms) — most stable for complex chains
```

**Latency formula:** `latency_ms = (samples / sampleRate) * 1000`

> Call before `begin()` to take effect immediately on startup, or after `begin()` to resize at runtime (causes a brief audio dropout).

---

### `process()`

Manually triggers one buffer cycle of DSP processing. Only needed if you have disabled the automatic background task.

```cpp
dsp.process();
```

In normal use, the background FreeRTOS task on Core 1 calls this automatically — you do **not** need to call it in `loop()` unless you have set `#define AUDIO_DSP_MANUAL_PROCESS` in `AudioConfig.h`.

```cpp
void loop() {
  // Only needed with AUDIO_DSP_MANUAL_PROCESS defined
  dsp.process();
}
```

---

### `clearEffects()`

Removes all effects from the processing chain and resets the signal path to pass-through.

```cpp
dsp.clearEffects();
```

Useful for switching presets at runtime without reinitializing the hardware:

```cpp
// Switch from clean to effected preset
dsp.clearEffects();
dsp.setDistortion(0.6f);
dsp.setLowPass(4000.0f, 1.0f);
```

---

## Filter Functions

All filters are implemented as **biquad IIR filters** — the standard building block for audio DSP. On ESP32, each biquad stage is accelerated by `dsps_biquad_f32()` from the `esp-dsp` library. On all other platforms, an equivalent portable C++ loop is used automatically.

**Q Factor reference:**

| Q Value | Character | Typical Use |
|---------|-----------|-------------|
| `0.5` | Very wide, gentle slope | Tone shaping |
| `0.707` | Butterworth — maximally flat | General purpose (default choice) |
| `1.0` | Moderate resonance | Musical filters |
| `1.5–2.0` | Noticeable resonance peak | Synth-style filters |
| `5.0+` | Sharp, narrow | Surgical EQ, special effects |

---

### `setLowPass()`

Attenuates all frequencies **above** the cutoff. Everything below passes through unchanged. Higher Q adds a resonance peak just below the cutoff frequency.

```cpp
dsp.setLowPass(float frequency_hz, float q_factor);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | Cutoff frequency in Hz |
| `q_factor` | `float` | `0.1`–`10.0` | Filter resonance (0.707 = flat) |

```cpp
dsp.setLowPass(800.0f, 0.707f);   // Muffled / underwater effect
dsp.setLowPass(200.0f, 1.5f);     // Boomy bass with slight resonance
dsp.setLowPass(8000.0f, 0.707f);  // Remove harsh high frequencies from a mix
```

**What you hear:** Cutting treble. The higher the Q, the more the cutoff frequency "rings."

---

### `setHighPass()`

Attenuates all frequencies **below** the cutoff. Everything above passes through. Essential for removing low-frequency noise, rumble, and proximity effect from microphones.

```cpp
dsp.setHighPass(float frequency_hz, float q_factor);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | Cutoff frequency in Hz |
| `q_factor` | `float` | `0.1`–`10.0` | Filter resonance (0.707 = flat) |

```cpp
dsp.setHighPass(80.0f, 0.707f);   // Remove mic handling noise and rumble
dsp.setHighPass(300.0f, 0.9f);    // Telephone effect (cuts bass entirely)
dsp.setHighPass(20.0f, 0.707f);   // Subsonic filter — protect speakers
```

**What you hear:** Cutting bass. The signal sounds thinner as cutoff increases.

---

### `setBandPass()`

Passes only the frequencies **around** the center frequency. Frequencies both above and below the band are attenuated.

```cpp
dsp.setBandPass(float frequency_hz, float bandwidth_hz);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | Center frequency of the passband in Hz |
| `bandwidth_hz` | `float` | `10`–`10000` | Width of the passband in Hz |

```cpp
dsp.setBandPass(1000.0f, 500.0f);   // Narrow band — lo-fi radio effect
dsp.setBandPass(1500.0f, 2000.0f);  // Wider band — telephone voice range
dsp.setBandPass(440.0f, 50.0f);     // Very narrow — isolate a single note
```

**What you hear:** The signal sounds hollow, nasal, or like a walkie-talkie depending on where the band sits.

---

### `setNotch()`

Removes a **single narrow frequency** while leaving everything else intact. Uses a very high-Q band-reject filter. The primary use case is eliminating mains hum (50 Hz in Europe, 60 Hz in the US).

```cpp
dsp.setNotch(float frequency_hz);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | The frequency to remove in Hz |

```cpp
dsp.setNotch(50.0f);   // Remove EU 230V mains hum
dsp.setNotch(60.0f);   // Remove US 120V mains hum
dsp.setNotch(100.0f);  // Remove mains harmonic (EU)
dsp.setNotch(1000.0f); // Remove a specific machine whine
```

**What you hear:** Almost nothing in most audio — the notch is extremely narrow. If the target frequency was prominent (like hum), it disappears cleanly.

> Stack multiple notch filters to remove a fundamental and its harmonics: `setNotch(50.0f)` + `setNotch(100.0f)` + `setNotch(150.0f)`.

---

### `setParametricEQ()`

Boosts or cuts a band of frequencies centered around a specific frequency. The width of the affected band is controlled by Q, and the amount of boost or cut is set in decibels. This is the most versatile filter in the library.

```cpp
dsp.setParametricEQ(float frequency_hz, float q_factor, float gain_db);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | Center frequency of the EQ band in Hz |
| `q_factor` | `float` | `0.1`–`10.0` | Width of the band (lower = wider) |
| `gain_db` | `float` | `-24.0`–`+24.0` | Amount of boost (+) or cut (−) in decibels |

```cpp
dsp.setParametricEQ(100.0f, 0.7f, +4.0f);   // Gentle bass boost
dsp.setParametricEQ(500.0f, 1.4f, -6.0f);   // Cut boxy mid frequencies
dsp.setParametricEQ(3000.0f, 1.4f, +3.0f);  // Presence boost for voice clarity
dsp.setParametricEQ(8000.0f, 0.9f, +2.0f);  // Air boost — add sparkle
dsp.setParametricEQ(200.0f, 1.0f, -3.0f);   // Low-mid cut — reduce muddiness
```

**What you hear:** Depends entirely on frequency and gain. Boosting 3–5 kHz adds vocal presence; cutting 200–400 Hz reduces muddiness; boosting 80–120 Hz adds warmth.

> Multiple `setParametricEQ()` calls build a **multi-band parametric EQ**. The ESP32-S3 can comfortably handle 8 bands simultaneously.

---

### `setLowShelf()`

Boosts or cuts all frequencies **below** the shelf frequency by a fixed amount. Unlike a parametric EQ, a shelf affects an entire region rather than a band.

```cpp
dsp.setLowShelf(float frequency_hz, float gain_db);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | Shelf transition frequency in Hz |
| `gain_db` | `float` | `-24.0`–`+24.0` | Gain applied below the shelf frequency |

```cpp
dsp.setLowShelf(200.0f, +4.0f);   // Bass boost below 200 Hz
dsp.setLowShelf(100.0f, -6.0f);   // Cut sub-bass for small speakers
dsp.setLowShelf(300.0f, +2.0f);   // Subtle warmth addition
```

**What you hear:** The bass region gets louder or quieter uniformly. Much gentler than a high-pass or low-pass filter.

---

### `setHighShelf()`

Boosts or cuts all frequencies **above** the shelf frequency by a fixed amount.

```cpp
dsp.setHighShelf(float frequency_hz, float gain_db);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `frequency_hz` | `float` | `20`–`(sampleRate/2)` | Shelf transition frequency in Hz |
| `gain_db` | `float` | `-24.0`–`+24.0` | Gain applied above the shelf frequency |

```cpp
dsp.setHighShelf(8000.0f, +3.0f);   // Air boost — add brightness
dsp.setHighShelf(6000.0f, -4.0f);   // De-ess / tame harsh highs
dsp.setHighShelf(10000.0f, +2.0f);  // Subtle presence lift
```

**What you hear:** The treble region gets brighter or darker uniformly.

---

## Dynamics Functions

Dynamics processors do not change which frequencies are present — they change the **level** of the signal based on how loud it is. Apply dynamics after your EQ filters for the most musical result.

---

### `setGain()`

Multiplies every sample in the buffer by a fixed linear factor. This is a simple volume control with no frequency-dependent behavior.

```cpp
dsp.setGain(float multiplier);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `multiplier` | `float` | `0.0`–`4.0` | Linear gain factor |

**dB conversion reference:**

| Multiplier | dB change |
|-----------|-----------|
| `0.25` | −12 dB |
| `0.5` | −6 dB |
| `1.0` | 0 dB (unity — no change) |
| `1.41` | +3 dB |
| `2.0` | +6 dB |
| `4.0` | +12 dB |

```cpp
dsp.setGain(0.5f);   // Halve the volume
dsp.setGain(1.0f);   // No change
dsp.setGain(2.0f);   // Double the volume — risk of clipping
```

> Values above `1.0` risk digital clipping. Place a `setLimiter()` after any `setGain()` above unity.

---

### `setCompressor()`

Automatically reduces the gain of loud signals that exceed a threshold. Signals below the threshold pass through unchanged. The result is a more consistent, controlled output level.

```cpp
dsp.setCompressor(float threshold, float ratio);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `threshold` | `float` | `0.0`–`1.0` | Signal level at which compression begins (fraction of full scale) |
| `ratio` | `float` | `1.0`–`20.0` | Compression ratio — how much louder signals are reduced |

**Ratio guide:**

| Ratio | Effect |
|-------|--------|
| `1.0` | No compression (bypass) |
| `2.0` | Gentle compression — barely noticeable |
| `4.0` | Standard vocal compression |
| `8.0` | Heavy compression — very controlled |
| `20.0` | Near-limiting — peaks are aggressively clamped |

```cpp
dsp.setCompressor(0.8f, 2.0f);   // Light glue compression
dsp.setCompressor(0.7f, 4.0f);   // Standard voice processing
dsp.setCompressor(0.5f, 8.0f);   // Heavy drum/instrument compression
```

**What you hear:** Loud moments get quieter without changing quiet moments. The result feels more even and "punchy" with the right settings.

---

### `setNoiseGate()`

Silences the output entirely when the input signal falls below a threshold. Eliminates background hiss, hum, and room noise between speech or instrument passages.

```cpp
dsp.setNoiseGate(float threshold);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `threshold` | `float` | `0.0`–`1.0` | Level below which the gate closes (fraction of full scale) |

```cpp
dsp.setNoiseGate(0.01f);  // Very sensitive — only closes on near-silence
dsp.setNoiseGate(0.02f);  // Recommended starting point for microphones
dsp.setNoiseGate(0.05f);  // Aggressive — suitable for loud, noisy environments
dsp.setNoiseGate(0.10f);  // Very aggressive — may cut off quiet speech
```

**What you hear:** Silence between words or notes instead of a noise floor. If set too high, the gate "chops" the beginning and end of sounds.

> Place the noise gate **before** any gain boost or distortion in the chain to prevent amplifying the noise you're trying to remove.

---

### `setLimiter()`

Prevents the output signal from exceeding a hard ceiling. Any sample that would exceed the ceiling is clipped to it exactly. Unlike a compressor, a limiter acts instantly and unconditionally.

```cpp
dsp.setLimiter(float ceiling);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `ceiling` | `float` | `0.0`–`1.0` | Maximum allowed output level (fraction of full scale) |

```cpp
dsp.setLimiter(0.99f);  // Protect against clipping at the DAC
dsp.setLimiter(0.95f);  // Recommended — 0.5 dB headroom
dsp.setLimiter(0.8f);   // Conservative ceiling — noticeably affects loud transients
```

**Best practice:** Always put a `setLimiter()` as the **last step** in any chain that includes gain boosts or distortion.

---

## Effect Functions

---

### `setDistortion()`

Applies hard or soft clipping to the signal, creating harmonic distortion. Used for guitar overdrive, fuzz, and lo-fi effects.

```cpp
dsp.setDistortion(float amount);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `amount` | `float` | `0.0`–`1.0` | Distortion intensity (0.0 = clean, 1.0 = heavily clipped) |

```cpp
dsp.setDistortion(0.1f);   // Subtle harmonic warmth
dsp.setDistortion(0.4f);   // Light overdrive — blues/rock tone
dsp.setDistortion(0.65f);  // Medium distortion — classic rock
dsp.setDistortion(0.9f);   // Heavy fuzz / metal
```

**Tip:** Put a high-pass filter before distortion to remove bass before clipping — this prevents low frequencies from creating an overly muddy distorted sound. Put a low-pass filter after distortion to tame the harsh upper harmonics.

```cpp
dsp.setHighPass(120.0f, 0.707f);  // Clean up the low end first
dsp.setGain(3.0f);                // Drive the signal harder into the clipper
dsp.setDistortion(0.6f);          // Clip
dsp.setLowPass(6000.0f, 0.707f);  // Roll off harsh harmonics
```

---

### `setBitCrusher()`

Reduces the effective bit depth of the audio by quantizing samples to fewer bits. This creates the characteristic gritty, lo-fi sound of early digital audio equipment and video game soundchips.

```cpp
dsp.setBitCrusher(int bits);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `bits` | `int` | `2`–`16` | Target bit depth (16 = transparent, lower = more effect) |

| Bits | Character |
|------|-----------|
| `16` | Transparent — no effect |
| `12` | Subtle graininess |
| `8` | Classic Game Boy / early CD-ROM |
| `6` | Atari-style crunch |
| `4` | Extreme lo-fi |
| `2` | Barely recognizable — almost square wave |

```cpp
dsp.setBitCrusher(12);  // Subtle vintage texture
dsp.setBitCrusher(8);   // Classic 8-bit game audio
dsp.setBitCrusher(4);   // Extreme degradation effect
```

**What you hear:** Quantization noise and a stepped, grainy quality that gets more extreme as bits decrease.

---

### `setDelay()`

Creates an echo effect by playing a delayed copy of the signal mixed back with the original. The feedback parameter controls how many times the echo repeats.

```cpp
dsp.setDelay(float delay_ms, float feedback);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `delay_ms` | `float` | `1`–`2000` | Delay time in milliseconds |
| `feedback` | `float` | `0.0`–`0.95` | How much of the delayed signal feeds back into the delay line |

```cpp
dsp.setDelay(50.0f, 0.0f);    // Slapback — single short echo (rockabilly guitar)
dsp.setDelay(250.0f, 0.4f);   // Quarter-note delay at 120 BPM with a few repeats
dsp.setDelay(500.0f, 0.6f);   // Long delay with many repeats
dsp.setDelay(20.0f, 0.5f);    // Very short delay — fattening / doubling effect
```

**Delay time to BPM formula:** `delay_ms = (60000 / BPM) / beat_subdivision`

> **Memory requirements:** The delay buffer lives in PSRAM. Delays above ~90 ms at 44100 Hz require PSRAM to be present (`#define BOARD_HAS_PSRAM` in `AudioConfig.h`). Without PSRAM, the maximum delay is limited to available heap.

> **Warning:** Never set `feedback` above `0.95` — values at or above `1.0` cause runaway feedback that will clip and potentially damage speakers.

---

### `setChorus()`

Creates a lush, wide sound by mixing the dry signal with one or more slightly pitch-modulated copies of itself. The pitch modulation is achieved by a slowly varying delay line driven by a low-frequency oscillator (LFO).

```cpp
dsp.setChorus(float rate_hz, float depth);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `rate_hz` | `float` | `0.1`–`5.0` | LFO speed in Hz (how fast the pitch wobbles) |
| `depth` | `float` | `0.0`–`1.0` | How much pitch variation is introduced |

```cpp
dsp.setChorus(0.5f, 0.2f);   // Subtle, natural widening
dsp.setChorus(1.2f, 0.3f);   // Classic chorus — lush and wide
dsp.setChorus(3.0f, 0.4f);   // Fast, vibrato-like modulation
dsp.setChorus(0.2f, 0.6f);   // Slow, deep — underwater feel
```

**What you hear:** The signal sounds wider and more three-dimensional. Higher depth settings introduce an audible pitch wobble (vibrato).

---

### `setReverb()`

Simulates the natural reflections of a physical space, making the signal sound as though it was recorded in a room, hall, or cave. Uses an algorithmic Schroeder/Moorer reverb network.

```cpp
dsp.setReverb(float room_size, float damping);
```

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `room_size` | `float` | `0.0`–`1.0` | Size of the simulated space (0.0 = tiny room, 1.0 = large hall) |
| `damping` | `float` | `0.0`–`1.0` | High-frequency absorption (0.0 = bright/reflective, 1.0 = dark/absorptive) |

```cpp
dsp.setReverb(0.2f, 0.5f);   // Small room — subtle ambience
dsp.setReverb(0.5f, 0.4f);   // Medium studio room
dsp.setReverb(0.8f, 0.3f);   // Large concert hall
dsp.setReverb(1.0f, 0.1f);   // Cathedral — very long, bright reverb
dsp.setReverb(0.6f, 0.9f);   // Large but dark — cave or bunker effect
```

**What you hear:** The signal gains a tail of decaying reflections. Higher room size = longer tail. Higher damping = the tail fades faster in the high frequencies (sounds warmer/darker).

> Reverb is the most CPU-intensive effect. On ESP32, it uses approximately 180 µs per 256-sample buffer. Avoid combining reverb with delay and chorus simultaneously on older ESP32 modules without PSRAM.

---

### Chaining Effects

Effects are applied strictly in the order they are added. The recommended signal chain order for most applications is:

```
Input → Noise Gate → High-Pass → EQ → Gain → Distortion → Low-Pass → Compressor → Reverb/Delay → Limiter → Output
```

Example — full vocal processing chain:

```cpp
dsp.setNoiseGate(0.02f);                          // 1. Kill background noise
dsp.setHighPass(80.0f, 0.707f);                   // 2. Remove rumble
dsp.setParametricEQ(200.0f, 1.0f, -3.0f);        // 3. Cut muddiness
dsp.setParametricEQ(3000.0f, 1.4f, +3.0f);       // 4. Add presence
dsp.setHighShelf(8000.0f, +2.0f);                 // 5. Air boost
dsp.setCompressor(0.7f, 4.0f);                    // 6. Control dynamics
dsp.setReverb(0.3f, 0.5f);                        // 7. Add room
dsp.setGain(1.2f);                                // 8. Output level trim
dsp.setLimiter(0.95f);                            // 9. Protect the DAC
```

Call `dsp.clearEffects()` to wipe the chain and start fresh.

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
