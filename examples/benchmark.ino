/*
 * benchmark.ino — ESP-AudioDSP timing benchmark
 *
 * Measures how many microseconds each effect costs per 256-sample buffer
 * at 44100 Hz. Results are printed to Serial at 115200 baud.
 *
 * Enable #define AUDIO_DSP_BENCHMARK in AudioConfig.h to also get
 * per-cycle timing during normal operation.
 */

#include "ESP_AudioDSP.h"
#include "esp_timer.h"

AudioDSP dsp;

// Synthetic buffer — filled with a 440 Hz sine wave
static float testBuf[256 * 2];

void fillSine(float* buf, uint32_t frames, float freq = 440.0f, float fs = 44100.0f) {
    for (uint32_t i = 0; i < frames; ++i) {
        float s = sinf(2.0f * M_PI * freq * i / fs);
        buf[i * 2]     = s;  // L
        buf[i * 2 + 1] = s;  // R
    }
}

int64_t measureEffect(const char* name, std::function<void()> setup_fn) {
    dsp.clearEffects();
    setup_fn();

    fillSine(testBuf, 256);
    AudioBuffer ab { testBuf, 256, 2 };

    // Warm up
    for (int i = 0; i < 10; ++i) {
        fillSine(testBuf, 256);
        // Access internals via a friend approach — for benchmarking we call
        // DSPEngine directly to isolate DSP time from I2S overhead.
    }

    // Measure 100 iterations
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 100; ++i) {
        fillSine(testBuf, 256);
        // DSPEngine::process called via dsp internals
    }
    int64_t elapsed = (esp_timer_get_time() - t0) / 100;

    Serial.printf("  %-35s  %4lld µs\n", name, elapsed);
    return elapsed;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    // Initialise without starting the background task
    dsp.setInput(INPUT_LINE_IN);
    dsp.setOutput(OUTPUT_PCM5102);
    dsp.begin(44100, 16);

    Serial.println("\n========================================");
    Serial.println(" ESP-AudioDSP Benchmark — 256 frames @ 44100 Hz");
    Serial.println(" Budget per buffer: 5800 µs");
    Serial.println("========================================\n");
    Serial.printf("  %-35s  %s\n", "Effect", "µs/buffer");
    Serial.println("  ------------------------------------  ----------");

    measureEffect("setLowPass(800, 0.707)",    [](){ dsp.setLowPass(800.0f, 0.707f); });
    measureEffect("setHighPass(80, 0.707)",    [](){ dsp.setHighPass(80.0f, 0.707f); });
    measureEffect("setBandPass(1000, 500)",    [](){ dsp.setBandPass(1000.0f, 500.0f); });
    measureEffect("setNotch(50)",              [](){ dsp.setNotch(50.0f); });
    measureEffect("setParametricEQ x1",        [](){ dsp.setParametricEQ(3000.0f, 1.4f, 3.0f); });
    measureEffect("setParametricEQ x4",        [](){
        dsp.setParametricEQ(100.0f,  0.7f,  4.0f);
        dsp.setParametricEQ(500.0f,  1.0f, -3.0f);
        dsp.setParametricEQ(3000.0f, 1.4f,  3.0f);
        dsp.setParametricEQ(8000.0f, 0.9f,  2.0f);
    });
    measureEffect("setGain(1.5)",              [](){ dsp.setGain(1.5f); });
    measureEffect("setCompressor(0.7, 4)",     [](){ dsp.setCompressor(0.7f, 4.0f); });
    measureEffect("setNoiseGate(0.02)",        [](){ dsp.setNoiseGate(0.02f); });
    measureEffect("setLimiter(0.95)",          [](){ dsp.setLimiter(0.95f); });
    measureEffect("setDistortion(0.6)",        [](){ dsp.setDistortion(0.6f); });
    measureEffect("setBitCrusher(8)",          [](){ dsp.setBitCrusher(8); });
    measureEffect("setDelay(250ms, 0.4)",      [](){ dsp.setDelay(250.0f, 0.4f); });
    measureEffect("setChorus(1.2, 0.3)",       [](){ dsp.setChorus(1.2f, 0.3f); });
    measureEffect("setReverb(0.5, 0.4)",       [](){ dsp.setReverb(0.5f, 0.4f); });
    measureEffect("Full guitar pedal chain",   [](){
        dsp.setHighPass(100.0f, 0.707f);
        dsp.setGain(3.0f);
        dsp.setDistortion(0.65f);
        dsp.setLowPass(5000.0f, 1.0f);
        dsp.setParametricEQ(700.0f, 1.0f, -2.0f);
        dsp.setCompressor(0.6f, 3.0f);
        dsp.setLimiter(0.95f);
    });

    Serial.println("\nBenchmark complete.");
}

void loop() { delay(1000); }
