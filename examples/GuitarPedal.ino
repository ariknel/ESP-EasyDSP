/*
 * GuitarPedal.ino — ESP-AudioDSP example
 *
 * Hardware: ESP32 + PCM5102A (DAC) + PCM1808 (ADC)
 *
 * A complete single-channel guitar overdrive pedal:
 *   1. High-pass to cut subsonic rumble
 *   2. Gain stage to drive into the clipper
 *   3. Distortion (soft/hard clip blend)
 *   4. Low-pass to tame harsh harmonics
 *   5. Mid scoop parametric EQ
 *   6. Compressor to even out the sustain
 *   7. Limiter to protect the DAC
 *
 * Three pots control Drive, Tone (cutoff), and Volume in real time.
 *
 * Wiring:
 *   PCM5102  BCLK → GPIO 25  |  LRCK → GPIO 26  |  DIN  → GPIO 22
 *   PCM1808  BCK  → GPIO 32  |  LRCK → GPIO 33  |  DOUT → GPIO 34
 *   Drive pot  → GPIO 34
 *   Tone  pot  → GPIO 35
 *   Volume pot → GPIO 36
 */

#include "ESP_AudioDSP.h"

AudioDSP dsp;

const int PIN_DRIVE  = 34;
const int PIN_TONE   = 35;
const int PIN_VOLUME = 36;

void buildChain(float drive, float tone, float volume) {
    dsp.clearEffects();

    dsp.setHighPass(100.0f, 0.707f);               // 1. Remove subsonic
    dsp.setGain(1.0f + drive * 4.0f);              // 2. Pre-gain (1x – 5x)
    dsp.setDistortion(drive);                       // 3. Clip
    dsp.setLowPass(2000.0f + tone * 6000.0f, 1.0f); // 4. Tone control: 2k–8k
    dsp.setParametricEQ(700.0f, 1.0f, -3.0f);      // 5. Scoop the boxy mid
    dsp.setCompressor(0.6f, 3.0f);                  // 6. Sustain compressor
    dsp.setGain(volume * 2.0f);                     // 7. Output volume
    dsp.setLimiter(0.95f);                          // 8. Protect DAC
}

void setup() {
    Serial.begin(115200);

    dsp.setInput(INPUT_LINE_IN);
    dsp.setOutput(OUTPUT_PCM5102);
    dsp.begin(44100, 24);

    buildChain(0.5f, 0.5f, 0.5f);  // Initial default settings
    Serial.println("GuitarPedal ready.");
}

void loop() {
    float drive  = analogRead(PIN_DRIVE)  / 4095.0f;
    float tone   = analogRead(PIN_TONE)   / 4095.0f;
    float volume = analogRead(PIN_VOLUME) / 4095.0f;

    buildChain(drive, tone, volume);

    Serial.printf("Drive: %.2f | Tone: %.2f | Volume: %.2f\n",
                  drive, tone, volume);
    delay(30);
}
