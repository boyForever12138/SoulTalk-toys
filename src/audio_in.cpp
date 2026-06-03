#include <driver/i2s.h>

#include "audio_in.h"
#include "config.h"
#include "pins.h"

namespace {
bool s_initialized = false;
constexpr i2s_port_t I2S_PORT_IN = I2S_NUM_1;
}  // namespace

namespace audio_in {

bool begin() {
  if (s_initialized)
    return true;

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = AUDIO_FRAME_SAMPLES,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {.bck_io_num = PIN_I2S_IN_SCK,
                                 .ws_io_num = PIN_I2S_IN_WS,
                                 .data_out_num = I2S_PIN_NO_CHANGE,
                                 .data_in_num = PIN_I2S_IN_SD};

  if (i2s_driver_install(I2S_PORT_IN, &i2s_config, 0, nullptr) != ESP_OK)
    return false;
  if (i2s_set_pin(I2S_PORT_IN, &pin_config) != ESP_OK)
    return false;

  s_initialized = true;
  return true;
}

void end() {
  if (!s_initialized)
    return;
  i2s_driver_uninstall(I2S_PORT_IN);
  s_initialized = false;
}

size_t read(int16_t *buf, size_t maxSamples, uint32_t timeoutMs) {
  if (!s_initialized)
    return 0;

  static int32_t tmp[AUDIO_FRAME_SAMPLES];
  size_t toRead = maxSamples;
  if (toRead > AUDIO_FRAME_SAMPLES)
    toRead = AUDIO_FRAME_SAMPLES;

  size_t bytesRead = 0;
  esp_err_t err = i2s_read(I2S_PORT_IN, tmp, toRead * sizeof(int32_t),
                           &bytesRead, pdMS_TO_TICKS(timeoutMs));
  if (err != ESP_OK)
    return 0;

  size_t got = bytesRead / sizeof(int32_t);
  // INMP441 24-bit data lives in upper bits. Right-shift 11 gives
  // a usable 16-bit value with reasonable headroom (tune 8..14 to taste).
  for (size_t i = 0; i < got; ++i) {
    int32_t v = tmp[i] >> 11;
    if (v > 32767)
      v = 32767;
    else if (v < -32768)
      v = -32768;
    buf[i] = (int16_t)v;
  }
  return got;
}

}  // namespace audio_in
