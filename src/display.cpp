#include <U8g2lib.h>
#include <Wire.h>

#include "display.h"
#include "pins.h"

namespace {
// 0.91" 128x32 SSD1306 over I2C
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

display_ui::State s_state = display_ui::State::Boot;
String s_lines[2];

const char* stateText(display_ui::State s) {
  switch (s) {
    case display_ui::State::Boot:
      return "Booting...";
    case display_ui::State::Provision:
      return "Setup AP";
    case display_ui::State::Connecting:
      return "WiFi/WS...";
    case display_ui::State::Pairing:
      return "Pair code:";
    case display_ui::State::Idle:
      return "Ready";
    case display_ui::State::Recording:
      return "REC *";
    case display_ui::State::Waiting:
      return "Thinking...";
    case display_ui::State::Playing:
      return "Playing >";
    case display_ui::State::Error:
      return "Error";
  }
  return "?";
}
}  // namespace

namespace display_ui {

void begin() {
  // U8g2 HW_I2C constructor calls Wire.begin() internally.
  // Configure pins BEFORE that so U8g2 uses the correct I2C bus.
  Serial.println("[display] setPins...");
  Serial.flush();
  Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);

  Serial.println("[display] u8g2.begin()...");
  Serial.flush();
  u8g2.begin();

  Serial.println("[display] setBusClock...");
  Serial.flush();
  u8g2.setBusClock(400000);

  Serial.println("[display] setFont...");
  Serial.flush();
  // wqy12 GB2312 font: covers ~6700 Chinese chars, ~12px height suits 128x32
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.enableUTF8Print();

  setLine(0, "SoulTalk Toy");
  setLine(1, "Booting...");

  Serial.println("[display] first render...");
  Serial.flush();
  render();
  Serial.println("[display] begin() done");
  Serial.flush();
}

void setState(State s) {
  s_state = s;
  setLine(0, "SoulTalk Toy");
  setLine(1, stateText(s));
  render();
}

void setLine(uint8_t row, const String& text) {
  if (row < 2)
    s_lines[row] = text;
}

void render() {
  u8g2.clearBuffer();
  // wqy12: ~12px height, baseline ~10. Two rows fit on 128x32 (rows at y=12,
  // y=28).
  u8g2.drawUTF8(0, 12, s_lines[0].c_str());
  u8g2.drawUTF8(0, 28, s_lines[1].c_str());
  u8g2.sendBuffer();
}

}  // namespace display_ui
