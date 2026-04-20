# ESP-AudioDSP

> Real-time audio filtering for ESP32 + PCM5102A DAC + PCM1808 ADC.  
> Add a parametric EQ, notch filter, or shelf in three lines of code.

[![License](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-red?style=flat-square)](https://www.espressif.com/)

---

## What it does

ESP-AudioDSP wraps Espressif's `esp-dsp` behind a clean API. You describe filters in human terms (Hz, Q, dB) — the library calculates biquad coefficients, runs SIMD-accelerated `dsps_biquad_f32()`, and handles all I2S DMA plumbing on Core 1.

```cpp
#include "ESP_AudioDSP.h"
AudioDSP dsp;

void setup() {
    dsp.begin(44100, 16);
    dsp.setInput(INPUT_I2S_ADC);
    dsp.setOutput(OUTPUT_PCM5102);
    dsp.addHighPass(80.f);
    dsp.addPeakEQ(3000.f, 1.4f, +3.f);
    dsp.start();
}
void loop() { }
```

---

## Current scope (v1.x) — filters only

Effects (delay, reverb, distortion, dynamics) are planned for v2.x.

| Feature | Status |
|---------|--------|
| Biquad filters (LP/HP/BP/Notch/PEQ/Shelf) | ✅ |
| SIMD acceleration via esp-dsp | ✅ |
| Live coefficient update (no dropout) | ✅ |
| I2S output — PCM5102A | ✅ |
| I2S input — PCM1808 | ✅ |
| Internal ADC input | ✅ |
| FreeRTOS task on Core 1 | ✅ |
| Dynamics / Effects | 🔜 v2 |

---

## Hardware

| Part | Role |
|------|------|
| ESP32 (any variant) | Microcontroller |
| PCM5102A | I2S DAC output |
| PCM1808 / PCM1802 | I2S ADC input |

### Default pins (edit AudioConfig.h to change)

```
PCM5102A   BCK → GPIO26   LRCK → GPIO25   DIN  → GPIO22
PCM1808    BCK → GPIO32   LRCK → GPIO33   DOUT → GPIO34
```

### PCM1808 required connections

```
FMT  → GND    (I2S format)
MD0  → GND    (slave mode)
MD1  → GND    (slave mode)
SCKI → 3.3V   (clock — or leave floating if module has oscillator)
```

### PCM5102A required connections

```
FLT  → GND    (normal roll-off)
DEMP → GND    (de-emphasis off)
XSMT → 3.3V   (UNMUTE — must be HIGH or output is silent)
FMT  → GND    (I2S standard format)
```

> ⚠️ `XSMT` is the most common first-time mistake. If you get silence, check this pin first.

---

## Installation

### Arduino IDE

1. Download ZIP → `Sketch → Include Library → Add .ZIP Library`
2. Install `esp-dsp` from Library Manager

### PlatformIO

```ini
lib_deps = https://github.com/your-handle/ESP-AudioDSP.git
```

---

## AudioConfig.h

```cpp
#define PIN_I2S_OUT_BCK   26
#define PIN_I2S_OUT_WS    25
#define PIN_I2S_OUT_DATA  22

#define PIN_I2S_IN_BCK    32
#define PIN_I2S_IN_WS     33
#define PIN_I2S_IN_DATA   34

#define PIN_ADC_IN        36   // internal ADC fallback

#define DMA_BUF_LEN    256    // frames per buffer (lower = less latency)
#define DMA_BUF_COUNT  4      // DMA descriptor count

#define MAX_BIQUAD_STAGES  8
```

---

## API Reference

### Setup

#### `begin(sampleRate, bitDepth)`
```cpp
dsp.begin(44100, 16);   // default — CD quality
dsp.begin(48000, 32);   // high resolution
```

#### `setInput(source)`
```cpp
dsp.setInput(INPUT_I2S_ADC);       // PCM1808 on I2S_NUM_1 (default)
dsp.setInput(INPUT_INTERNAL_ADC);  // ESP32 GPIO36, 12-bit, ~60dB SNR
```

#### `setOutput(dest)`
```cpp
dsp.setOutput(OUTPUT_PCM5102);  // PCM5102A on I2S_NUM_0 (only option currently)
```

#### `setOutputGain(gain)`
Linear multiplier applied after all filters. Default 1.0.
```cpp
dsp.setOutputGain(0.5f);   // −6 dB
dsp.setOutputGain(2.0f);   // +6 dB
```

#### `start()` → bool
Inits I2S and starts DSP task on Core 1. Returns false on hardware error.

#### `stop()`
Stops task and uninstalls I2S drivers.

---

### Filter functions

All return an `int` stage index for live updates. Stages apply in add order.

---

#### `addLowPass(freq, Q=0.707)` → int
Attenuates above cutoff.
```cpp
int idx = dsp.addLowPass(1000.f);         // 1 kHz, flat
dsp.addLowPass(500.f, 1.5f);             // resonant
```
**Q:** 0.5 = gentle · 0.707 = flat (default) · 1.5+ = resonant peak

---

#### `addHighPass(freq, Q=0.707)` → int
Attenuates below cutoff.
```cpp
dsp.addHighPass(80.f);           // remove rumble
dsp.addHighPass(300.f, 0.9f);    // telephone cut
```

---

#### `addBandPass(freq, bandwidthHz)` → int
Passes only a band around center. `Q = freq / bwHz` internally.
```cpp
dsp.addBandPass(1000.f, 500.f);   // passes 750–1250 Hz
```

---

#### `addNotch(freq)` → int
Surgical narrow null (Q=30). Use for mains hum removal.
```cpp
dsp.addNotch(50.f);   // EU hum
dsp.addNotch(60.f);   // US hum
```

---

#### `addPeakEQ(freq, Q, dBgain)` → int
Boost or cut a band. Stack multiple for a full parametric EQ.
```cpp
dsp.addPeakEQ(3000.f, 1.4f, +3.f);   // presence boost
dsp.addPeakEQ(200.f,  1.0f, -4.f);   // cut muddiness
```
`dBgain` range: ±24 dB

---

#### `addLowShelf(freq, dBgain)` → int
Boost/cut all frequencies **below** shelf point.
```cpp
dsp.addLowShelf(200.f, +4.f);    // bass boost
dsp.addLowShelf(100.f, -6.f);    // cut sub-bass
```

---

#### `addHighShelf(freq, dBgain)` → int
Boost/cut all frequencies **above** shelf point.
```cpp
dsp.addHighShelf(8000.f, +3.f);   // add brightness
dsp.addHighShelf(6000.f, -4.f);   // tame harsh highs
```

---

### Live update functions

Change parameters at runtime — no dropout, no state reset. Thread-safe.

```cpp
int lpIdx = dsp.addLowPass(1000.f);

// In loop() — sweep from a pot:
dsp.updateLowPass(lpIdx, newFreq);
```

| Function | Parameters |
|----------|------------|
| `updateLowPass(idx, freq, Q)` | Q optional, default 0.707 |
| `updateHighPass(idx, freq, Q)` | Q optional, default 0.707 |
| `updateBandPass(idx, freq, bwHz)` | |
| `updateNotch(idx, freq)` | |
| `updatePeakEQ(idx, freq, Q, dBgain)` | |
| `updateLowShelf(idx, freq, dBgain)` | |
| `updateHighShelf(idx, freq, dBgain)` | |

---

#### `clearFilters()`
Removes all stages. Audio continues as pass-through.

#### `filterCount()` → int
Returns number of active stages.

#### `printStatus()`
Prints one-line summary to Serial.

---

## Examples

### BasicTest
`examples/BasicTest/BasicTest.ino`

**Start here.** Pass-through with a 2 kHz LP filter that toggles every 5 seconds. Use to verify your wiring before anything else.

### AuxFilter
`examples/AuxFilter/AuxFilter.ino`

Live 3-band EQ (Bass/Mid/Treble) with three potentiometers. GPIO35/34/39, ±12 dB range each.

---

## Known issues — first hardware run

These are the most likely problems you'll hit:

| # | Symptom | Likely cause | Fix |
|---|---------|-------------|-----|
| 1 | Complete silence from DAC | `XSMT` pin on PCM5102A is LOW | Pull `XSMT` HIGH (to 3.3V) |
| 2 | Distorted audio with 16-bit | PCM1808 outputs 24-bit packed in 32 | Try `dsp.begin(44100, 32)` |
| 3 | Filters sound inverted | `a1/a2` sign mismatch in esp-dsp version | In `FilterChain.h`, remove the `-` negation on `a1`/`a2` |
| 4 | Regular clicking | DMA buffer underrun | Set `DMA_BUF_COUNT 8` in AudioConfig.h |
| 5 | ESP32 resets (stack overflow) | DSP task stack too small | Set `DSP_TASK_STACK 8192` in AudioConfig.h |
| 6 | Internal ADC very noisy | WiFi/BT interference | Add `WiFi.mode(WIFI_OFF)` in setup() |
| 7 | No I2S input | PCM1808 FMT/MD0/MD1 not grounded | Ground all three |

---

## Roadmap

- [x] v1.0 — Filter chain, I2S I/O, FreeRTOS task, live updates
- [ ] v1.1 — Gain stage, limiter, mono/stereo routing
- [ ] v2.0 — Dynamics (compressor, gate)
- [ ] v2.1 — Effects (delay, reverb, distortion) + PSRAM
- [ ] v3.0 — WiFi control, SPIFFS presets

---

## License

MIT — see [LICENSE](LICENSE).

**Acknowledgements:** [espressif/esp-dsp](https://github.com/espressif/esp-dsp) · [Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) — R. Bristow-Johnson
