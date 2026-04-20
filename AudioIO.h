#pragma once
#include <stdint.h>
#include <string.h>
#include "driver/i2s.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "AudioTypes.h"
#include "../AudioConfig.h"

static const char* AIO_TAG = "AudioIO";

// =============================================================================
// AudioIO.h  —  ESP32 I2S driver wrapper + int↔float conversion
//
// Ports used:
//   I2S_NUM_0  →  output  (PCM5102A DAC)
//   I2S_NUM_1  →  input   (PCM1808 ADC or I2S mic)
//
// All audio inside the library is stereo-interleaved normalised float
// in the range −1.0 … +1.0.  Integer↔float conversion happens here,
// at the hardware boundary, and nowhere else.
//
// Bit-depth notes:
//   - "16-bit" mode:  ESP32 I2S uses int16_t words.  PCM5102A happily accepts
//     this.  PCM1808 in 16-bit mode works fine too.
//
//   - "32-bit" mode:  Needed if PCM1808 is set to 24-bit output (the chip
//     packs 24-bit data left-justified in a 32-bit I2S word).
//     With 32-bit mode:  sample = raw_int32 / 2^31
//     The bottom 8 bits from PCM1808 will be noise — that is normal.
//
// If you get distorted / wrong-pitched audio, try switching bitDepth:
//   dsp.begin(44100, 32);
// =============================================================================

class AudioIO {
public:

    // =========================================================================
    //  Initialisation
    // =========================================================================

    // -------------------------------------------------------------------------
    // Initialise I2S output  (I2S_NUM_0  →  PCM5102A)
    // -------------------------------------------------------------------------
    bool beginOutput(uint32_t sampleRate, uint8_t bitDepth) {
        _sampleRate = sampleRate;
        _bitDepth   = bitDepth;

        i2s_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
        cfg.sample_rate          = sampleRate;
        cfg.bits_per_sample      = (i2s_bits_per_sample_t)bitDepth;
        cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
        cfg.dma_buf_count        = DMA_BUF_COUNT;
        cfg.dma_buf_len          = DMA_BUF_LEN;
        cfg.use_apll             = true;   // APLL gives accurate audio sample rate
        cfg.tx_desc_auto_clear   = true;   // output silence on underrun

        esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(AIO_TAG, "I2S_NUM_0 install failed: %s", esp_err_to_name(err));
            return false;
        }

        i2s_pin_config_t pins;
        memset(&pins, 0, sizeof(pins));
        pins.bck_io_num   = PIN_I2S_OUT_BCK;
        pins.ws_io_num    = PIN_I2S_OUT_WS;
        pins.data_out_num = PIN_I2S_OUT_DATA;
        pins.data_in_num  = I2S_PIN_NO_CHANGE;

        err = i2s_set_pin(I2S_NUM_0, &pins);
        if (err != ESP_OK) {
            ESP_LOGE(AIO_TAG, "I2S_NUM_0 set_pin failed: %s", esp_err_to_name(err));
            return false;
        }

        i2s_zero_dma_buffer(I2S_NUM_0);
        _outReady = true;
        ESP_LOGI(AIO_TAG, "Output ready. SR=%lu BD=%d BCK=%d WS=%d DATA=%d",
                 (unsigned long)sampleRate, bitDepth,
                 PIN_I2S_OUT_BCK, PIN_I2S_OUT_WS, PIN_I2S_OUT_DATA);
        return true;
    }

    // -------------------------------------------------------------------------
    // Initialise I2S input  (I2S_NUM_1  →  PCM1808)
    // -------------------------------------------------------------------------
    bool beginI2SInput(uint32_t sampleRate, uint8_t bitDepth) {
        i2s_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
        cfg.sample_rate          = sampleRate;
        cfg.bits_per_sample      = (i2s_bits_per_sample_t)bitDepth;
        cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
        cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
        cfg.dma_buf_count        = DMA_BUF_COUNT;
        cfg.dma_buf_len          = DMA_BUF_LEN;
        cfg.use_apll             = true;
        cfg.tx_desc_auto_clear   = false;

        esp_err_t err = i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
        if (err != ESP_OK) {
            ESP_LOGE(AIO_TAG, "I2S_NUM_1 install failed: %s", esp_err_to_name(err));
            return false;
        }

        i2s_pin_config_t pins;
        memset(&pins, 0, sizeof(pins));
        pins.bck_io_num   = PIN_I2S_IN_BCK;
        pins.ws_io_num    = PIN_I2S_IN_WS;
        pins.data_out_num = I2S_PIN_NO_CHANGE;
        pins.data_in_num  = PIN_I2S_IN_DATA;

        err = i2s_set_pin(I2S_NUM_1, &pins);
        if (err != ESP_OK) {
            ESP_LOGE(AIO_TAG, "I2S_NUM_1 set_pin failed: %s", esp_err_to_name(err));
            return false;
        }

        _inReady  = true;
        _inSource = INPUT_I2S_ADC;
        ESP_LOGI(AIO_TAG, "Input ready (I2S). BCK=%d WS=%d DATA=%d",
                 PIN_I2S_IN_BCK, PIN_I2S_IN_WS, PIN_I2S_IN_DATA);
        return true;
    }

    // -------------------------------------------------------------------------
    // Initialise ESP32 internal ADC input (single-ended, mono→stereo copy)
    //
    // The internal ADC is 12-bit with ~60 dB SNR.
    // Input must be biased to 0–3.3 V (centre at 1.65 V for bipolar audio).
    // IMPORTANT: disable WiFi before using internal ADC for audio.
    // -------------------------------------------------------------------------
    bool beginInternalADC(adc1_channel_t channel = ADC1_CHANNEL_0) {
        _adcChannel = channel;
        adc1_config_width(ADC_WIDTH_BIT_12);
        // ADC_ATTEN_DB_11 allows up to ~3.9V input (full rail at 3.3V is safe)
        adc1_config_channel_atten(channel, ADC_ATTEN_DB_11);
        _inReady  = true;
        _inSource = INPUT_INTERNAL_ADC;
        ESP_LOGI(AIO_TAG, "Input ready (internal ADC). Channel=%d", channel);
        return true;
    }

    // =========================================================================
    //  Read / Write
    // =========================================================================

    // -------------------------------------------------------------------------
    // Read one buffer of stereo interleaved float samples from the input.
    //
    // @param dst     Output buffer, must hold (frames * 2) floats
    // @param frames  Number of stereo frames to read
    //
    // Returns actual frames read. On the first few calls this may be less
    // than requested while DMA buffers fill; the calling loop will catch up.
    // -------------------------------------------------------------------------
    uint32_t read(float* dst, uint32_t frames) {
        if (!_inReady) {
            memset(dst, 0, frames * 2 * sizeof(float));
            return frames;
        }
        if (_inSource == INPUT_I2S_ADC) {
            return readI2S(dst, frames);
        } else {
            return readInternalADC(dst, frames);
        }
    }

    // -------------------------------------------------------------------------
    // Write one buffer of stereo interleaved float samples to I2S TX.
    //
    // @param src     Input buffer: [L0,R0,L1,R1,...], length = frames * 2
    // @param frames  Number of stereo frames
    // -------------------------------------------------------------------------
    void write(const float* src, uint32_t frames) {
        if (!_outReady) return;

        const uint32_t totalSamples = frames * 2;
        size_t written = 0;

        if (_bitDepth == 16) {
            // 16-bit path — int16 on stack, 256 frames * 2 ch * 2 bytes = 1 kB
            int16_t ibuf[DMA_BUF_LEN * 2];
            for (uint32_t i = 0; i < totalSamples; ++i) {
                float s  = src[i] < -1.f ? -1.f : src[i] > 1.f ? 1.f : src[i];
                ibuf[i]  = (int16_t)(s * 32767.f);
            }
            i2s_write(I2S_NUM_0, ibuf, totalSamples * sizeof(int16_t),
                      &written, portMAX_DELAY);
        } else {
            // 32-bit path — int32 on stack, 256 frames * 2 ch * 4 bytes = 2 kB
            int32_t ibuf[DMA_BUF_LEN * 2];
            for (uint32_t i = 0; i < totalSamples; ++i) {
                float s  = src[i] < -1.f ? -1.f : src[i] > 1.f ? 1.f : src[i];
                ibuf[i]  = (int32_t)(s * 2147483647.f);   // INT32_MAX
            }
            i2s_write(I2S_NUM_0, ibuf, totalSamples * sizeof(int32_t),
                      &written, portMAX_DELAY);
        }
    }

    // =========================================================================
    //  Accessors
    // =========================================================================
    bool     outputReady() const { return _outReady;    }
    bool     inputReady()  const { return _inReady;     }
    uint32_t sampleRate()  const { return _sampleRate;  }
    uint8_t  bitDepth()    const { return _bitDepth;    }

private:
    uint32_t       _sampleRate  = 44100;
    uint8_t        _bitDepth    = 16;
    bool           _outReady    = false;
    bool           _inReady     = false;
    InputSource    _inSource    = INPUT_I2S_ADC;
    adc1_channel_t _adcChannel  = ADC1_CHANNEL_0;

    // -------------------------------------------------------------------------
    // Read from I2S_NUM_1 (PCM1808) into normalised float
    // -------------------------------------------------------------------------
    uint32_t readI2S(float* dst, uint32_t frames) {
        const uint32_t totalSamples = frames * 2;
        size_t bytesRead = 0;

        if (_bitDepth == 16) {
            int16_t ibuf[DMA_BUF_LEN * 2];
            i2s_read(I2S_NUM_1, ibuf, totalSamples * sizeof(int16_t),
                     &bytesRead, portMAX_DELAY);
            uint32_t got = bytesRead / sizeof(int16_t);
            for (uint32_t i = 0; i < got; ++i)
                dst[i] = ibuf[i] * (1.f / 32768.f);
            // Zero-pad if short read (should not happen in steady state)
            for (uint32_t i = got; i < totalSamples; ++i)
                dst[i] = 0.f;
            return got / 2;
        } else {
            // 32-bit — covers PCM1808 24-bit data packed in 32-bit words
            int32_t ibuf[DMA_BUF_LEN * 2];
            i2s_read(I2S_NUM_1, ibuf, totalSamples * sizeof(int32_t),
                     &bytesRead, portMAX_DELAY);
            uint32_t got = bytesRead / sizeof(int32_t);
            for (uint32_t i = 0; i < got; ++i)
                dst[i] = ibuf[i] * (1.f / 2147483648.f);   // / 2^31
            for (uint32_t i = got; i < totalSamples; ++i)
                dst[i] = 0.f;
            return got / 2;
        }
    }

    // -------------------------------------------------------------------------
    // Read from ESP32 internal ADC — mono, duplicated to stereo
    // Each sample requires ~20 µs at 12-bit resolution.
    // At DMA_BUF_LEN=256 that is 5.1 ms for one buffer — matches the I2S rate.
    // -------------------------------------------------------------------------
    uint32_t readInternalADC(float* dst, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) {
            int   raw = adc1_get_raw(_adcChannel);      // 0–4095
            float s   = (raw - 2048) * (1.f / 2048.f); // centre around 0, range ±1
            dst[i * 2]     = s;   // Left
            dst[i * 2 + 1] = s;   // Right (copy)
        }
        return frames;
    }
};
