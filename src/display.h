#pragma once
#include <Arduino.h>

namespace display_ui {
enum class State {
  Boot,
  Provision,
  Connecting,
  Pairing,
  Idle,
  Recording,
  Waiting,
  Playing,
  Error
};

void begin();
void setState(State s);
void setLine(uint8_t row, const String &text);
void render();
}  // namespace display_ui
