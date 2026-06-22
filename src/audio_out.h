#pragma once
#include <Arduino.h>

namespace audio_out {
    bool begin();   // MAX98357A on I2S0
    void end();
    // Write 16-bit mono PCM @ 16kHz. Returns samples written.
    size_t write(const int16_t *buf, size_t samples, uint32_t timeoutMs = 100);
    // Queue 16-bit mono PCM for the background playback task. Returns samples accepted.
    size_t enqueue(const int16_t *buf, size_t samples);
    // Mark the current stream complete so a short final buffer can drain below prebuffer.
    void finish();
    void reset();
    size_t bufferedSamples();
    bool isActive();
    bool isDrained();
    void logStats();
    void mute(); // write a short silent block to drain
}
