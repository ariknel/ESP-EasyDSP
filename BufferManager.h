#pragma once
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../AudioConfig.h"

// =============================================================================
// BufferManager.h — Ping-pong buffer manager
//
// Two float buffers (A and B) alternate roles:
//   - The FILL buffer receives fresh samples from I2S RX (or silence).
//   - The PLAY buffer is being read by the DSP task and written to I2S TX.
//
// A binary semaphore signals the DSP task that a new buffer is ready.
// Swap happens atomically — no locks needed during processing.
// =============================================================================

class BufferManager {
public:

    bool begin(uint32_t bufSize, uint8_t channels = 2) {
        _bufSize  = bufSize;
        _channels = channels;
        uint32_t floats = bufSize * channels;

        _bufA = new float[floats];
        _bufB = new float[floats];
        if (!_bufA || !_bufB) return false;

        memset(_bufA, 0, floats * sizeof(float));
        memset(_bufB, 0, floats * sizeof(float));

        _fillBuf = _bufA;
        _playBuf = _bufB;

        _sem = xSemaphoreCreateBinary();
        return (_sem != nullptr);
    }

    void end() {
        delete[] _bufA; _bufA = nullptr;
        delete[] _bufB; _bufB = nullptr;
        if (_sem) { vSemaphoreDelete(_sem); _sem = nullptr; }
    }

    // -------------------------------------------------------------------------
    // Called from the I2S capture side (or fill loop):
    // Copy incoming data into the fill buffer, then swap and signal DSP task.
    // -------------------------------------------------------------------------
    void submitFill(const float* src, uint32_t frames) {
        uint32_t n = frames * _channels;
        memcpy(_fillBuf, src, n * sizeof(float));
        swap();
        xSemaphoreGive(_sem);
    }

    // Fill with silence (pass-through / output-only mode)
    void submitSilence() {
        memset(_fillBuf, 0, _bufSize * _channels * sizeof(float));
        swap();
        xSemaphoreGive(_sem);
    }

    // -------------------------------------------------------------------------
    // Called from the DSP task:
    // Block until a buffer is ready, then return a pointer to the play buffer.
    // -------------------------------------------------------------------------
    float* waitForBuffer(TickType_t timeout = portMAX_DELAY) {
        if (xSemaphoreTake(_sem, timeout) == pdTRUE)
            return _playBuf;
        return nullptr;
    }

    // Direct access for the DSP task after waitForBuffer()
    float*   playBuf()  const { return _playBuf;  }
    float*   fillBuf()  const { return _fillBuf;  }
    uint32_t bufSize()  const { return _bufSize;  }
    uint8_t  channels() const { return _channels; }

private:
    float*           _bufA     = nullptr;
    float*           _bufB     = nullptr;
    float*           _fillBuf  = nullptr;  // being written into
    float*           _playBuf  = nullptr;  // being processed / output
    uint32_t         _bufSize  = AUDIO_BUFFER_SIZE;
    uint8_t          _channels = 2;
    SemaphoreHandle_t _sem     = nullptr;

    // Atomic pointer swap — called only from the fill side
    void swap() {
        float* tmp = _fillBuf;
        _fillBuf   = _playBuf;
        _playBuf   = tmp;
    }
};
