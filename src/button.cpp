#include "button.h"
#include "pins.h"
#include "config.h"

namespace {
struct ButtonState {
    bool lastRaw = true;       // pulled-up = released
    bool stable = true;
    uint32_t lastChangeMs = 0;
    uint32_t pressStartMs = 0;
    bool longFired = false;
};

ButtonState s_ptt;
ButtonState s_setup;

button::Event pollPin(uint8_t pin, ButtonState &state) {
    bool raw = digitalRead(pin);
    uint32_t now = millis();

    if (raw != state.lastRaw) {
        state.lastRaw = raw;
        state.lastChangeMs = now;
    }

    button::Event ev = button::Event::None;
    if ((now - state.lastChangeMs) >= BTN_DEBOUNCE_MS &&
        raw != state.stable) {
        state.stable = raw;
        if (state.stable == LOW) {
            state.pressStartMs = now;
            state.longFired = false;
            ev = button::Event::Pressed;
        } else {
            ev = button::Event::Released;
        }
    }

    if (state.stable == LOW && !state.longFired &&
        (now - state.pressStartMs) >= BTN_LONGPRESS_MS) {
        state.longFired = true;
        ev = button::Event::LongPress;
    }

    return ev;
}
}

namespace button {

void begin() {
    pinMode(PIN_BUTTON_PTT, INPUT_PULLUP);
    pinMode(PIN_BUTTON_SETUP, INPUT_PULLUP);
    s_ptt.lastRaw = digitalRead(PIN_BUTTON_PTT);
    s_ptt.stable = s_ptt.lastRaw;
    s_setup.lastRaw = digitalRead(PIN_BUTTON_SETUP);
    s_setup.stable = s_setup.lastRaw;
}

Event poll() {
    return pollPin(PIN_BUTTON_PTT, s_ptt);
}

Event pollSetup() {
    return pollPin(PIN_BUTTON_SETUP, s_setup);
}

bool isHeld() {
    return s_ptt.stable == LOW;
}

bool isSetupHeld() {
    return s_setup.stable == LOW;
}

} // namespace
