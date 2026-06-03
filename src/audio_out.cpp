#include "audio_out.h"
#include "pins.h"
#include "config.h"
#include <driver/i2s_std.h>
#include <string.h>

namespace {
    i2s_chan_handle_t s_tx = nullptr;
}

namespace audio_out {

bool begin() {
    if (s_tx) return true;

    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&cfg, &s_tx, nullptr) != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_I2S_OUT_BCLK,
            .ws   = (gpio_num_t)PIN_I2S_OUT_LRC,
            .dout = (gpio_num_t)PIN_I2S_OUT_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {0, 0, 0},
        },
    };

    if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) return false;
    if (i2s_channel_enable(s_tx) != ESP_OK) return false;
    return true;
}

void end() {
    if (!s_tx) return;
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = nullptr;
}

size_t write(const int16_t *buf, size_t samples, uint32_t timeoutMs) {
    if (!s_tx) return 0;
    size_t bytesWritten = 0;
    esp_err_t err = i2s_channel_write(s_tx, buf, samples * sizeof(int16_t),
                                      &bytesWritten, pdMS_TO_TICKS(timeoutMs));
    if (err != ESP_OK) return 0;
    return bytesWritten / sizeof(int16_t);
}

void mute() {
    static int16_t silent[64] = {0};
    write(silent, 64, 20);
}

} // namespace
