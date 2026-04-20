/*
 * BasicTest.ino  –  ESP-AudioDSP
 * ============================================================
 * The simplest possible test sketch.
 *
 * What it does:
 *   1. Pass audio from I2S ADC (PCM1808) through to PCM5102A DAC
 *   2. Applies one low-pass filter at 2000 Hz
 *   3. Prints status to Serial every 2 seconds
 *   4. Every 5 seconds, toggles between filtered and pass-through
 *      so you can clearly hear the difference
 *
 * Hardware:
 *   ESP32 devkit + PCM5102A DAC + PCM1808 ADC
 *   Pins are defined in AudioConfig.h  (defaults below)
 *
 *   PCM5102A:  BCK → GPIO26   LRCK → GPIO25   DIN  → GPIO22
 *   PCM1808:   BCK → GPIO32   LRCK → GPIO33   DOUT → GPIO34
 *
 *   Also connect PCM1808:
 *     FMT  → GND  (I2S format)
 *     MD0  → GND, MD1 → GND  (slave mode)
 *     SCKI → 3.3V or connect to ESP32 MCLK if needed
 *
 * If you do NOT have a PCM1808, change setInput to INPUT_INTERNAL_ADC
 * and connect an audio source to GPIO36.
 *
 * Serial: 115200 baud
 * ============================================================
 */

#include "ESP_AudioDSP.h"

AudioDSP dsp;

int  lpStageIdx = -1;    // index of our low-pass stage
bool filterOn   = true;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP-AudioDSP BasicTest ===");

    // 1. Configure
    dsp.begin(44100, 16);
    dsp.setInput(INPUT_I2S_ADC);      // change to INPUT_INTERNAL_ADC if no PCM1808
    dsp.setOutput(OUTPUT_PCM5102);

    // 2. Add a 2 kHz low-pass filter
    lpStageIdx = dsp.addLowPass(2000.f, 0.707f);
    Serial.printf("Low-pass stage index: %d\n", lpStageIdx);

    // 3. Start the DSP task on Core 1
    if (!dsp.start()) {
        Serial.println("ERROR: DSP start failed. Check wiring and AudioConfig.h");
        while (true) delay(1000);
    }

    Serial.println("DSP running. You should hear low-pass filtered audio.");
    Serial.println("Filter will toggle every 5 seconds.\n");
}

void loop() {
    static uint32_t lastToggle = 0;
    static uint32_t lastStatus = 0;

    uint32_t now = millis();

    // Print status every 2 seconds
    if (now - lastStatus >= 2000) {
        lastStatus = now;
        dsp.printStatus();
        Serial.printf("Filter is currently: %s\n", filterOn ? "ON (2kHz LP)" : "OFF (pass-through)");
    }

    // Toggle filter every 5 seconds
    if (now - lastToggle >= 5000) {
        lastToggle = now;
        filterOn = !filterOn;

        if (filterOn) {
            // Restore the low-pass
            dsp.updateLowPass(lpStageIdx, 2000.f, 0.707f);
            Serial.println(">> Filter ON  (2 kHz low-pass)");
        } else {
            // Set cutoff very high = effectively bypass
            // (Cleaner than clearFilters() which would cause a brief click)
            dsp.updateLowPass(lpStageIdx, 20000.f, 0.707f);
            Serial.println(">> Filter OFF (20 kHz low-pass = pass-through)");
        }
    }
}
