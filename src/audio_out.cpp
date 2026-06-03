#include <driver/i2s.h>
#include <string.h>

#include "audio_out.h"
#include "config.h"
#include "pins.h"

namespace {
bool s_initialized = false;
constexpr i2s_port_t I2S_PORT_OUT = I2S_NUM_0;
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
      .dma_buf_count = 4,
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

  s_initialized = true;
  return true;
}

void end() {
  if (!s_initialized)
    return;
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

}  // namespace audio_out
