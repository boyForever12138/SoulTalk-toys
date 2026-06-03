#pragma once
#include <Arduino.h>

namespace audio_in {
    bool begin();   // INMP441 on I2S1
    void end();
    // Read up to maxSamples 16-bit mono PCM @ 16kHz into buffer.
    // Blocks until something is read or timeout (ms).
    // Returns number of samples read.
    size_t read(int16_t *buf, size_t maxSamples, uint32_t timeoutMs = 100);
}
