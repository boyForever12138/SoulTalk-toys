#pragma once

// ===== ESP32-S3 N16R8 pin map (per wiring plan) =====

// MAX98357A (I2S0 OUT)
#define PIN_I2S_OUT_BCLK   16
#define PIN_I2S_OUT_LRC    17
#define PIN_I2S_OUT_DIN    15

// INMP441 (I2S1 IN). On the chip, I2S "BCLK" connects to mic SCK.
#define PIN_I2S_IN_SCK     5   // mic SCK
#define PIN_I2S_IN_WS      4   // mic WS
#define PIN_I2S_IN_SD      6   // mic SD (data out -> ESP DIN)

// SSD1306 OLED (I2C)
#define PIN_I2C_SCL        2
#define PIN_I2C_SDA        42

// PTT button: GPIO7 -> GND, with internal pull-up
#define PIN_BUTTON_PTT     7
