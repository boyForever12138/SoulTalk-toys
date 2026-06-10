#pragma once
#include <Arduino.h>

namespace audio_out {
bool begin();  // MAX98357A on I2S0
void end();
// Write 16-bit mono PCM @ 16kHz. Returns samples written.
size_t write(const int16_t *buf, size_t samples, uint32_t timeoutMs = 100);
void mute();  // write a short silent block to drain

// Push PCM bytes into playback ring buffer (non-blocking, returns bytes pushed)
size_t push(const uint8_t *data, size_t len, uint32_t timeoutMs = 0);
// Call from loop() to continuously drain ring buffer to I2S during playback
void update();
// Clear ring buffer (call when playback session ends)
void clear();
// Stop playback immediately and reset state (called when starting new
// recording)
void stop();
// Print ring buffer stats every ~2s (for debugging)
void printStats();
// Play a test tone (sine wave) to verify speaker is working.
// freqHz: tone frequency, durationMs: how long to play
void testTone(int freqHz = 1000, uint32_t durationMs = 2000);
// Check if ring buffer is empty (used to determine when to stop playback)
bool isBufferEmpty();
}  // namespace audio_out
