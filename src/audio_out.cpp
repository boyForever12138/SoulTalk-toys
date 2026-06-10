#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#include "audio_out.h"
#include "config.h"
#include "pins.h"

namespace {
bool s_initialized = false;
constexpr i2s_port_t I2S_PORT_OUT = I2S_NUM_0;

// Ring buffer for smooth audio playback (~1s @ 16kHz/16bit/mono)
static constexpr size_t RING_SIZE_BYTES = 32768;
static uint8_t s_ringBuf[RING_SIZE_BYTES];
static size_t s_ringRead = 0;
static size_t s_ringWrite = 0;
static size_t s_ringAvail = 0;
static size_t s_peakAvail = 0;     // high water mark for debugging
static size_t s_bytesDropped = 0;  // bytes dropped when buffer full
static size_t s_totalPushed = 0;   // total bytes successfully pushed
static size_t s_totalDrained = 0;  // total bytes drained to I2S
static uint32_t s_lastStatMs = 0;
static bool s_playing = false;  // true when actively draining

// Semaphore to signal the drain task when new data is available
static SemaphoreHandle_t s_drainSem = nullptr;
static TaskHandle_t s_drainTaskHandle = nullptr;

// Drain task: runs on its own core, continuously drains ring buffer to I2S
void drainTask(void *) {
  while (true) {
    // Wait for signal that data is available
    xSemaphoreTake(s_drainSem, portMAX_DELAY);

    while (s_playing && s_ringAvail >= 64) {
      size_t samples = s_ringAvail / 2;
      size_t maxSamples = AUDIO_FRAME_SAMPLES;
      if (samples > maxSamples)
        samples = maxSamples;
      size_t bytes = samples * 2;

      // Handle wraparound
      int16_t tmp[AUDIO_FRAME_SAMPLES];
      size_t offset = s_ringRead;
      if (offset + bytes <= RING_SIZE_BYTES) {
        memcpy(tmp, s_ringBuf + offset, bytes);
      } else {
        size_t first = RING_SIZE_BYTES - offset;
        memcpy(tmp, s_ringBuf + offset, first);
        memcpy((uint8_t *)tmp + first, s_ringBuf, bytes - first);
      }

      // Block until I2S accepts the data. This naturally paces at real-time
      // playback rate (32KB/s for 16kHz/16bit/mono) because each write of
      // 320 samples takes ~20ms to play through DMA.
      size_t written = audio_out::write(tmp, samples, pdMS_TO_TICKS(1000));
      if (written > 0) {
        s_ringRead = (s_ringRead + written * 2) % RING_SIZE_BYTES;
        s_ringAvail -= written * 2;
        s_totalDrained += written * 2;
      } else {
        // I2S didn't accept data even after 1s timeout - yield to avoid
        // starving the scheduler, then retry
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
  }
}

// Push PCM data into ring buffer (returns bytes pushed, may be less than len if
// full)
size_t ringPush(const uint8_t *data, size_t len) {
  if (len == 0)
    return 0;
  size_t space = RING_SIZE_BYTES - s_ringAvail;
  if (space == 0) {
    s_bytesDropped += len;
    return 0;
  }
  if (len > space) {
    s_bytesDropped += (len - space);
    len = space;
  }
  size_t n = len;
  while (n > 0) {
    size_t chunk = RING_SIZE_BYTES - s_ringWrite;
    if (chunk > n)
      chunk = n;
    memcpy(s_ringBuf + s_ringWrite, data, chunk);
    s_ringWrite = (s_ringWrite + chunk) % RING_SIZE_BYTES;
    data += chunk;
    n -= chunk;
  }
  s_ringAvail += len;
  s_totalPushed += len;
  if (s_ringAvail > s_peakAvail)
    s_peakAvail = s_ringAvail;
  // Signal the drain task that new data is available
  if (s_drainSem)
    xSemaphoreGive(s_drainSem);
  return len;
}

// Clear ring buffer (call when playback ends)
void ringClear() {
  s_ringRead = 0;
  s_ringWrite = 0;
  s_ringAvail = 0;
  s_playing = false;
}
}  // namespace

namespace audio_out {

bool begin() {
  if (s_initialized)
    return true;

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = AUDIO_FRAME_SAMPLES,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {.bck_io_num = PIN_I2S_OUT_BCLK,
                                 .ws_io_num = PIN_I2S_OUT_LRC,
                                 .data_out_num = PIN_I2S_OUT_DIN,
                                 .data_in_num = I2S_PIN_NO_CHANGE};

  if (i2s_driver_install(I2S_PORT_OUT, &i2s_config, 0, nullptr) != ESP_OK)
    return false;
  if (i2s_set_pin(I2S_PORT_OUT, &pin_config) != ESP_OK)
    return false;

  // Create drain task on core 1 (free core when WiFi runs on core 0)
  s_drainSem = xSemaphoreCreateCounting(10, 0);
  if (s_drainSem) {
    xTaskCreatePinnedToCore(drainTask, "audio_drain", 4096, nullptr, 5,
                            &s_drainTaskHandle, 1);
  }

  s_initialized = true;
  return true;
}

void end() {
  if (!s_initialized)
    return;
  s_playing = false;
  if (s_drainSem) {
    xSemaphoreGive(s_drainSem);  // wake task so it can check s_playing
  }
  vTaskDelay(pdMS_TO_TICKS(50));  // give task time to stop
  if (s_drainTaskHandle) {
    vTaskDelete(s_drainTaskHandle);
    s_drainTaskHandle = nullptr;
  }
  if (s_drainSem) {
    vSemaphoreDelete(s_drainSem);
    s_drainSem = nullptr;
  }
  i2s_driver_uninstall(I2S_PORT_OUT);
  s_initialized = false;
}

size_t write(const int16_t *buf, size_t samples, uint32_t timeoutMs) {
  if (!s_initialized)
    return 0;
  size_t bytesWritten = 0;
  esp_err_t err = i2s_write(I2S_PORT_OUT, buf, samples * sizeof(int16_t),
                            &bytesWritten, pdMS_TO_TICKS(timeoutMs));
  if (err != ESP_OK)
    return 0;
  return bytesWritten / sizeof(int16_t);
}

void mute() {
  static int16_t silent[64] = {0};
  write(silent, 64, 20);
}

size_t push(const uint8_t *data, size_t len, uint32_t timeoutMs) {
  if (!s_initialized)
    return 0;
  s_playing = true;  // activate drain task
  return ringPush(data, len);
}

void update() {
  // No-op: drain is handled by dedicated FreeRTOS task
}

void clear() {
  s_playing = false;
  ringClear();
}

void stop() {
  s_playing = false;
  ringClear();
  // Send silence to flush I2S pipeline
  static int16_t silent[64] = {0};
  write(silent, 64, 20);
}

void printStats() {
  uint32_t now = millis();
  if (now - s_lastStatMs < 2000)
    return;
  s_lastStatMs = now;
  Serial.printf(
      "[audio] ring: avail=%zu/%zu peak=%zu pushed=%zu drained=%zu "
      "dropped=%zu\n",
      s_ringAvail, RING_SIZE_BYTES, s_peakAvail, s_totalPushed, s_totalDrained,
      s_bytesDropped);
}

void testTone(int freqHz, uint32_t durationMs) {
  if (!s_initialized)
    return;
  Serial.printf("[audio] Playing %dHz test tone for %ums\n", freqHz,
                durationMs);

  // Generate sine wave in frames
  constexpr size_t FRAME = 160;  // 10ms worth at 16kHz
  int16_t buf[FRAME];
  double phase = 0.0;
  double phaseStep = (2.0 * M_PI * freqHz) / AUDIO_SAMPLE_RATE;

  uint32_t start = millis();
  while (millis() - start < durationMs) {
    for (size_t i = 0; i < FRAME; i++) {
      buf[i] = (int16_t)(sin(phase) * 8000.0);  // 50% amplitude
      phase += phaseStep;
      if (phase >= 2.0 * M_PI)
        phase -= 2.0 * M_PI;
    }
    write(buf, FRAME, 50);
  }
  Serial.println("[audio] Test tone done");
}

bool isBufferEmpty() {
  return s_ringAvail == 0;
}

}  // namespace audio_out
