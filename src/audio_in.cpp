#include "audio_in.h"
#include "pins.h"
#include "config.h"
#include <driver/i2s_std.h>

namespace {
    i2s_chan_handle_t s_rx = nullptr;
}

namespace audio_in {

bool begin() {
    if (s_rx) return true;

    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&cfg, nullptr, &s_rx) != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_I2S_IN_SCK,
            .ws   = (gpio_num_t)PIN_I2S_IN_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)PIN_I2S_IN_SD,
            .invert_flags = {0, 0, 0},
        },
    };
    // INMP441 outputs left-channel data when L/R = GND
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    if (i2s_channel_init_std_mode(s_rx, &std_cfg) != ESP_OK) return false;
    if (i2s_channel_enable(s_rx) != ESP_OK) return false;
    return true;
}

void end() {
    if (!s_rx) return;
    i2s_channel_disable(s_rx);
    i2s_del_channel(s_rx);
    s_rx = nullptr;
}

size_t read(int16_t *buf, size_t maxSamples, uint32_t timeoutMs) {
    if (!s_rx) return 0;
    // INMP441 ships 32-bit frames; we read 32-bit then shift.
    static int32_t tmp[AUDIO_FRAME_SAMPLES];
    size_t toRead = maxSamples;
    if (toRead > AUDIO_FRAME_SAMPLES) toRead = AUDIO_FRAME_SAMPLES;

    size_t bytesRead = 0;
    esp_err_t err = i2s_channel_read(s_rx, tmp, toRead * sizeof(int32_t),
                                     &bytesRead, pdMS_TO_TICKS(timeoutMs));
    if (err != ESP_OK) return 0;
    size_t got = bytesRead / sizeof(int32_t);
    // Shift: INMP441 24-bit data lives in upper bits. Right-shift 11 gives
    // a usable 16-bit value with reasonable headroom (tune 8..14 to taste).
    for (size_t i = 0; i < got; ++i) {
        int32_t v = tmp[i] >> 11;
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
    return got;
}

} // namespace
