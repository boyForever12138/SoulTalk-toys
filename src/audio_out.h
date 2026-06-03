#pragma once
#include <Arduino.h>

namespace audio_out {
    bool begin();   // MAX98357A on I2S0
    void end();
    // Write 16-bit mono PCM @ 16kHz. Returns samples written.
    size_t write(const int16_t *buf, size_t samples, uint32_t timeoutMs = 100);
    void mute(); // write a short silent block to drain
}
