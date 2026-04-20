/*
 * AuxFilter.ino  –  ESP-AudioDSP
 * ============================================================
 * Live 3-band EQ on an aux (line-level) input via PCM1808 ADC,
 * output to PCM5102A DAC.
 * Three pots control Bass / Mid / Treble in real time.
 *
 * Hardware:
 *   ESP32 devkit + PCM5102A + PCM1808 + 3x 10k pots
 *
 *   PCM5102A:  BCK  → GPIO26   LRCK → GPIO25   DIN  → GPIO22
 *   PCM1808:   BCK  → GPIO32   LRCK → GPIO33   DOUT → GPIO34
 *              FMT  → GND      MD0  → GND       MD1  → GND
 *
 *   Pot BASS   → GPIO35   (centre wiper; outer pins to 3.3V and GND)
 *   Pot MID    → GPIO34
 *   Pot TREBLE → GPIO39
 *
 * No PCM1808?  Change INPUT_I2S_ADC → INPUT_INTERNAL_ADC and
 * feed your audio source into GPIO36 via a bias circuit (see README).
 *
 * Serial: 115200 baud
 * ============================================================
 */

#include "ESP_AudioDSP.h"

#define POT_BASS    35
#define POT_MID     34
#define POT_TREBLE  39

#define FREQ_BASS    250.f
#define FREQ_MID    1000.f
#define FREQ_TREBLE 6000.f
#define EQ_Q         1.0f
#define MAX_DB      12.0f

AudioDSP dsp;

int stageBass   = -1;
int stageMid    = -1;
int stageTreble = -1;

float smoothBass   = 0.f;
float smoothMid    = 0.f;
float smoothTreble = 0.f;

float adcTodB(int raw) {
    return ((raw / 4095.f) - 0.5f) * 2.f * MAX_DB;
}

float expSmooth(float cur, float target) {
    return cur + 0.05f * (target - cur);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== AuxFilter – 3-band EQ ===");

    dsp.begin(44100, 16);
    dsp.setInput(INPUT_I2S_ADC);
    dsp.setOutput(OUTPUT_PCM5102);

    // Build EQ chain – start flat (0 dB gain)
    stageBass   = dsp.addLowShelf (FREQ_BASS,   0.f);
    stageMid    = dsp.addPeakEQ   (FREQ_MID, EQ_Q, 0.f);
    stageTreble = dsp.addHighShelf(FREQ_TREBLE, 0.f);
    dsp.addHighPass(20.f);   // DC block – always on

    if (!dsp.start()) {
        Serial.println("ERROR: start() failed – check wiring / AudioConfig.h");
        while (true) delay(1000);
    }

    Serial.printf("Running. Stages: bass=%d mid=%d treble=%d\n",
                  stageBass, stageMid, stageTreble);
}

void loop() {
    // Read pots (0-4095) → dB gain
    smoothBass   = expSmooth(smoothBass,   adcTodB(analogRead(POT_BASS)));
    smoothMid    = expSmooth(smoothMid,    adcTodB(analogRead(POT_MID)));
    smoothTreble = expSmooth(smoothTreble, adcTodB(analogRead(POT_TREBLE)));

    // Hot-swap coefficients (mutex-protected inside AudioDSP)
    dsp.updateLowShelf (stageBass,   FREQ_BASS,   smoothBass);
    dsp.updatePeakEQ   (stageMid,    FREQ_MID,    EQ_Q, smoothMid);
    dsp.updateHighShelf(stageTreble, FREQ_TREBLE, smoothTreble);

    static uint32_t lastPrint = 0;
    if (millis() - lastPrint >= 500) {
        lastPrint = millis();
        Serial.printf("Bass %+.1fdB  Mid %+.1fdB  Treble %+.1fdB\n",
                      smoothBass, smoothMid, smoothTreble);
    }

    delay(20);
}
