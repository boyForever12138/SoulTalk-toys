#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "audio_out.h"
#include "config.h"
#include "pins.h"

namespace {
bool s_initialized = false;
constexpr i2s_port_t I2S_PORT_OUT = I2S_NUM_0;

constexpr size_t kRingSamples =
    (AUDIO_SAMPLE_RATE * AUDIO_OUT_BUFFER_MS) / 1000;
constexpr size_t kPrebufferSamples =
    (AUDIO_SAMPLE_RATE * AUDIO_OUT_PREBUFFER_MS) / 1000;
constexpr size_t kPlayFrameSamples = AUDIO_FRAME_SAMPLES;

int16_t *s_ring = nullptr;
size_t s_head = 0;
size_t s_tail = 0;
size_t s_count = 0;
size_t s_peak = 0;
uint32_t s_pushed = 0;
uint32_t s_drained = 0;
uint32_t s_dropped = 0;
uint32_t s_underruns = 0;
bool s_playbackActive = false;
bool s_taskRunning = false;
TaskHandle_t s_task = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

size_t directWrite(const int16_t *buf, size_t samples, uint32_t timeoutMs) {
  if (!s_initialized)
    return 0;
  size_t bytesWritten = 0;
  esp_err_t err = i2s_write(I2S_PORT_OUT, buf, samples * sizeof(int16_t),
                            &bytesWritten, pdMS_TO_TICKS(timeoutMs));
  if (err != ESP_OK)
    return 0;
  return bytesWritten / sizeof(int16_t);
}

size_t copyFromRing(int16_t *out, size_t samples) {
  portENTER_CRITICAL(&s_mux);
  size_t n = samples;
  if (n > s_count)
    n = s_count;

  size_t first = n;
  if (first > kRingSamples - s_tail)
    first = kRingSamples - s_tail;
  if (first > 0)
    memcpy(out, s_ring + s_tail, first * sizeof(int16_t));

  size_t second = n - first;
  if (second > 0)
    memcpy(out + first, s_ring, second * sizeof(int16_t));

  s_tail = (s_tail + n) % kRingSamples;
  s_count -= n;
  s_drained += n;
  portEXIT_CRITICAL(&s_mux);
  return n;
}

size_t currentBufferedSamples() {
  portENTER_CRITICAL(&s_mux);
  size_t out = s_count;
  portEXIT_CRITICAL(&s_mux);
  return out;
}

void setPlaybackActive(bool active) {
  portENTER_CRITICAL(&s_mux);
  s_playbackActive = active;
  portEXIT_CRITICAL(&s_mux);
}

void playbackTask(void *) {
  int16_t frame[kPlayFrameSamples];
  memset(frame, 0, sizeof(frame));

  while (s_taskRunning) {
    size_t buffered = currentBufferedSamples();
    if (!s_playbackActive) {
      if (buffered < kPrebufferSamples) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      setPlaybackActive(true);
    }

    size_t got = copyFromRing(frame, kPlayFrameSamples);
    if (got == 0) {
      portENTER_CRITICAL(&s_mux);
      s_playbackActive = false;
      s_underruns++;
      portEXIT_CRITICAL(&s_mux);
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    if (got < kPlayFrameSamples) {
      memset(frame + got, 0, (kPlayFrameSamples - got) * sizeof(int16_t));
    }
    directWrite(frame, kPlayFrameSamples, 1000);
  }

  s_task = nullptr;
  vTaskDelete(nullptr);
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

  if (!s_ring) {
    s_ring = (int16_t *)heap_caps_malloc(kRingSamples * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_ring) {
      s_ring = (int16_t *)heap_caps_malloc(kRingSamples * sizeof(int16_t),
                                           MALLOC_CAP_8BIT);
    }
  }
  if (!s_ring)
    return false;

  reset();

  s_initialized = true;
  s_taskRunning = true;
  if (xTaskCreatePinnedToCore(playbackTask, "audio_out", 4096, nullptr, 2,
                              &s_task, 1) != pdPASS) {
    s_taskRunning = false;
    return false;
  }
  Serial.printf("[audio_out] buffer=%u samples (%u ms), prebuffer=%u samples\n",
                (unsigned)kRingSamples, (unsigned)AUDIO_OUT_BUFFER_MS,
                (unsigned)kPrebufferSamples);
  return true;
}

void end() {
  if (!s_initialized)
    return;
  s_taskRunning = false;
  vTaskDelay(pdMS_TO_TICKS(30));
  i2s_driver_uninstall(I2S_PORT_OUT);
  s_initialized = false;
}

size_t write(const int16_t *buf, size_t samples, uint32_t timeoutMs) {
  return directWrite(buf, samples, timeoutMs);
}

size_t enqueue(const int16_t *buf, size_t samples) {
  if (!s_initialized || !s_ring || !buf || samples == 0)
    return 0;

  size_t inputOffset = 0;
  size_t toWrite = samples;

  portENTER_CRITICAL(&s_mux);
  if (toWrite > kRingSamples) {
    size_t dropFromInput = toWrite - kRingSamples;
    inputOffset += dropFromInput;
    toWrite = kRingSamples;
    s_dropped += dropFromInput;
  }

  size_t freeSamples = kRingSamples - s_count;
  if (toWrite > freeSamples) {
    size_t dropOld = toWrite - freeSamples;
    s_tail = (s_tail + dropOld) % kRingSamples;
    s_count -= dropOld;
    s_dropped += dropOld;
  }

  size_t first = toWrite;
  if (first > kRingSamples - s_head)
    first = kRingSamples - s_head;
  if (first > 0)
    memcpy(s_ring + s_head, buf + inputOffset, first * sizeof(int16_t));

  size_t second = toWrite - first;
  if (second > 0)
    memcpy(s_ring, buf + inputOffset + first, second * sizeof(int16_t));

  s_head = (s_head + toWrite) % kRingSamples;
  s_count += toWrite;
  if (s_count > s_peak)
    s_peak = s_count;
  s_pushed += toWrite;
  portEXIT_CRITICAL(&s_mux);
  return toWrite;
}

void reset() {
  portENTER_CRITICAL(&s_mux);
  s_head = 0;
  s_tail = 0;
  s_count = 0;
  s_peak = 0;
  s_pushed = 0;
  s_drained = 0;
  s_dropped = 0;
  s_underruns = 0;
  s_playbackActive = false;
  portEXIT_CRITICAL(&s_mux);
}

size_t bufferedSamples() {
  return currentBufferedSamples();
}

bool isActive() {
  portENTER_CRITICAL(&s_mux);
  bool active = s_playbackActive;
  portEXIT_CRITICAL(&s_mux);
  return active;
}

bool isDrained() {
  portENTER_CRITICAL(&s_mux);
  bool drained = s_count == 0 && !s_playbackActive;
  portEXIT_CRITICAL(&s_mux);
  return drained;
}

void logStats() {
  portENTER_CRITICAL(&s_mux);
  size_t count = s_count;
  size_t peak = s_peak;
  uint32_t pushed = s_pushed;
  uint32_t drained = s_drained;
  uint32_t dropped = s_dropped;
  uint32_t underruns = s_underruns;
  bool active = s_playbackActive;
  portEXIT_CRITICAL(&s_mux);
  Serial.printf(
      "[audio] ring: avail=%u/%u peak=%u pushed=%u drained=%u dropped=%u underruns=%u active=%d\n",
      (unsigned)count, (unsigned)kRingSamples, (unsigned)peak,
      (unsigned)pushed, (unsigned)drained, (unsigned)dropped,
      (unsigned)underruns, active ? 1 : 0);
}

void mute() {
  static int16_t silent[64] = {0};
  directWrite(silent, 64, 20);
}

}  // namespace audio_out
