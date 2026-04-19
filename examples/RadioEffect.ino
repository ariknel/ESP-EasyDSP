/*
 * RadioEffect.ino — ESP-AudioDSP example
 *
 * Hardware: ESP32 + PCM5102A (DAC) + I2S MEMS microphone (e.g. INMP441)
 *
 * Simulates a walkie-talkie / AM radio voice:
 *   - High-pass removes bass entirely (speech only: 300 Hz+)
 *   - Band-pass narrows the response to the telephone frequency range
 *   - Soft distortion adds that characteristic "crackle"
 *   - Notch removes any 50 Hz hum picked up by the mic
 *   - Noise gate silences between transmissions
 *
 * The effect can be toggled on/off with a pushbutton on GPIO 0 (BOOT button).
 *
 * Wiring:
 *   PCM5102  BCLK → GPIO 25  |  LRCK → GPIO 26  |  DIN  → GPIO 22
 *   INMP441  SCK  → GPIO 32  |  WS   → GPIO 33  |  SD   → GPIO 34
 *   Button   → GPIO 0 (active LOW, uses internal pull-up)
 */

#include "ESP_AudioDSP.h"

AudioDSP dsp;

const int BUTTON_PIN = 0;
bool effectEnabled   = true;

void applyRadioEffect() {
    dsp.clearEffects();
    dsp.setHighPass(300.0f, 0.9f);          // Cut all bass
    dsp.setBandPass(1400.0f, 1800.0f);      // Telephone band: 500–2300 Hz
    dsp.setDistortion(0.25f);               // Subtle crackle
    dsp.setNotch(50.0f);                    // Kill mains hum
    dsp.setNoiseGate(0.015f);              // Silence between words
    dsp.setGain(2.0f);                      // Compensate for band-pass level loss
    dsp.setLimiter(0.95f);
}

void applyBypass() {
    dsp.clearEffects();
    dsp.setLimiter(0.95f);  // Pass-through with clipping protection
}

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    dsp.setInput(INPUT_I2S_MIC);
    dsp.setOutput(OUTPUT_PCM5102);
    dsp.begin(44100, 16);

    applyRadioEffect();
    Serial.println("RadioEffect running. Press BOOT button to toggle.");
}

void loop() {
    // Toggle effect on button press (debounced)
    static bool lastBtn   = HIGH;
    static uint32_t lastT = 0;
    bool btn = digitalRead(BUTTON_PIN);

    if (btn == LOW && lastBtn == HIGH && (millis() - lastT) > 50) {
        effectEnabled = !effectEnabled;
        lastT = millis();
        if (effectEnabled) {
            applyRadioEffect();
            Serial.println("Radio effect ON");
        } else {
            applyBypass();
            Serial.println("Radio effect OFF (bypass)");
        }
    }
    lastBtn = btn;
    delay(10);
}
