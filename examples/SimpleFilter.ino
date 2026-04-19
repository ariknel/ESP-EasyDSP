/*
 * SimpleFilter.ino — ESP-AudioDSP example
 *
 * Hardware: ESP32 + PCM5102A (DAC) + PCM1808 (ADC)
 *
 * Demonstrates a basic low-pass filter on a line-level input.
 * The pot on GPIO 35 sweeps the cutoff from 200 Hz to 8000 Hz in real time.
 *
 * Wiring (defaults from AudioConfig.h):
 *   PCM5102  BCLK → GPIO 25  |  LRCK → GPIO 26  |  DIN  → GPIO 22
 *   PCM1808  BCK  → GPIO 32  |  LRCK → GPIO 33  |  DOUT → GPIO 34
 *   Pot wiper → GPIO 35 (ADC1_CH7)
 */

#include "ESP_AudioDSP.h"

AudioDSP dsp;

const int POT_PIN    = 35;
const float FREQ_MIN = 200.0f;
const float FREQ_MAX = 8000.0f;

void setup() {
    Serial.begin(115200);

    dsp.setInput(INPUT_LINE_IN);
    dsp.setOutput(OUTPUT_PCM5102);
    dsp.begin(44100, 16);

    Serial.println("SimpleFilter running. Turn the pot to sweep cutoff frequency.");
}

void loop() {
    // Read pot and map to cutoff frequency
    float raw    = analogRead(POT_PIN) / 4095.0f;
    float cutoff = FREQ_MIN + raw * (FREQ_MAX - FREQ_MIN);

    // Rebuild the filter with the new frequency every 50 ms
    dsp.clearEffects();
    dsp.setLowPass(cutoff, 0.707f);
    dsp.setLimiter(0.95f);

    Serial.printf("Cutoff: %.0f Hz\n", cutoff);
    delay(50);
}
