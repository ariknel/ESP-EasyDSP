#pragma once
#include <stdint.h>
#include "driver/i2s.h"
#include "AudioTypes.h"
#include "../AudioConfig.h"

// =============================================================================
// I2S_HAL.h — ESP32 I2S hardware abstraction layer
//
// Manages two I2S ports:
//   I2S_NUM_0  → output (PCM5102 DAC)
//   I2S_NUM_1  → input  (PCM1808 ADC or I2S mic)
//
// Converts between int32_t DMA frames and normalised float buffers.
// =============================================================================

class I2S_HAL {
public:

    // -------------------------------------------------------------------------
    // Initialise the output port (I2S_NUM_0)
    // -------------------------------------------------------------------------
    bool beginOutput(uint32_t sampleRate, uint8_t bitDepth,
                     int bclk = I2S_OUT_BCLK,
                     int lrck = I2S_OUT_LRCK,
                     int data = I2S_OUT_DATA)
    {
        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate          = sampleRate,
            .bits_per_sample      = bitsToEnum(bitDepth),
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count        = 4,
            .dma_buf_len          = AUDIO_BUFFER_SIZE,
            .use_apll             = true,
            .tx_desc_auto_clear   = true,
            .fixed_mclk           = 0
        };

        i2s_pin_config_t pins = {
            .bck_io_num   = bclk,
            .ws_io_num    = lrck,
            .data_out_num = data,
            .data_in_num  = I2S_PIN_NO_CHANGE
        };

        esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
        if (err != ESP_OK) return false;
        err = i2s_set_pin(I2S_NUM_0, &pins);
        if (err != ESP_OK) return false;

        i2s_zero_dma_buffer(I2S_NUM_0);
        _sampleRate = sampleRate;
        _bitDepth   = bitDepth;
        return true;
    }

    // -------------------------------------------------------------------------
    // Initialise the input port (I2S_NUM_1)
    // -------------------------------------------------------------------------
    bool beginInput(uint32_t sampleRate, uint8_t bitDepth,
                    int bclk = I2S_IN_BCLK,
                    int lrck = I2S_IN_LRCK,
                    int data = I2S_IN_DATA)
    {
        i2s_config_t cfg = {
            .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate          = sampleRate,
            .bits_per_sample      = bitsToEnum(bitDepth),
            .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count        = 4,
            .dma_buf_len          = AUDIO_BUFFER_SIZE,
            .use_apll             = true,
            .tx_desc_auto_clear   = false,
            .fixed_mclk           = 0
        };

        i2s_pin_config_t pins = {
            .bck_io_num   = bclk,
            .ws_io_num    = lrck,
            .data_out_num = I2S_PIN_NO_CHANGE,
            .data_in_num  = data
        };

        esp_err_t err = i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
        if (err != ESP_OK) return false;
        err = i2s_set_pin(I2S_NUM_1, &pins);
        return (err == ESP_OK);
    }

    // -------------------------------------------------------------------------
    // Read one buffer of frames from I2S RX into a normalised float array.
    // outFloat must have space for (frames * 2) floats (stereo interleaved).
    // Returns number of frames actually read.
    // -------------------------------------------------------------------------
    uint32_t readFloat(float* outFloat, uint32_t frames) {
        const uint32_t bytesWanted = frames * 2 * (_bitDepth / 8);
        size_t bytesRead = 0;

        // Intermediate int32 buffer on stack (max 512 frames * 2ch * 4 bytes = 4kB)
        static int32_t intBuf[AUDIO_BUFFER_SIZE * 2];

        i2s_read(I2S_NUM_1, intBuf, bytesWanted, &bytesRead, portMAX_DELAY);
        uint32_t samplesRead = bytesRead / (_bitDepth / 8);

        float scale = 1.0f / (float)(1 << (_bitDepth - 1));
        for (uint32_t i = 0; i < samplesRead; ++i)
            outFloat[i] = (float)intBuf[i] * scale;

        return samplesRead / 2;  // frames (2 channels per frame)
    }

    // -------------------------------------------------------------------------
    // Write one buffer of normalised float samples to I2S TX.
    // inFloat is stereo interleaved (frames * 2 samples).
    // -------------------------------------------------------------------------
    void writeFloat(const float* inFloat, uint32_t frames) {
        const uint32_t totalSamples = frames * 2;
        static int32_t intBuf[AUDIO_BUFFER_SIZE * 2];

        float scale = (float)((1 << (_bitDepth - 1)) - 1);
        for (uint32_t i = 0; i < totalSamples; ++i) {
            float clamped = inFloat[i] < -1.0f ? -1.0f :
                            inFloat[i] >  1.0f ?  1.0f : inFloat[i];
            intBuf[i] = (int32_t)(clamped * scale);
        }

        size_t written = 0;
        i2s_write(I2S_NUM_0, intBuf,
                  totalSamples * (_bitDepth / 8),
                  &written, portMAX_DELAY);
    }

    void stop() {
        i2s_driver_uninstall(I2S_NUM_0);
        i2s_driver_uninstall(I2S_NUM_1);
    }

    uint32_t sampleRate() const { return _sampleRate; }
    uint8_t  bitDepth()   const { return _bitDepth;   }

private:
    uint32_t _sampleRate = 44100;
    uint8_t  _bitDepth   = 16;

    static i2s_bits_per_sample_t bitsToEnum(uint8_t bits) {
        switch (bits) {
            case 32: return I2S_BITS_PER_SAMPLE_32BIT;
            case 24: return I2S_BITS_PER_SAMPLE_24BIT;
            default: return I2S_BITS_PER_SAMPLE_16BIT;
        }
    }
};
