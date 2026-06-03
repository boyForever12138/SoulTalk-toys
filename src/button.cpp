#include "button.h"
#include "pins.h"
#include "config.h"

namespace {
    bool s_lastRaw = true;       // pulled-up = released
    bool s_stable = true;
    uint32_t s_lastChangeMs = 0;
    uint32_t s_pressStartMs = 0;
    bool s_longFired = false;
}

namespace button {

void begin() {
    pinMode(PIN_BUTTON_PTT, INPUT_PULLUP);
    s_lastRaw = digitalRead(PIN_BUTTON_PTT);
    s_stable = s_lastRaw;
}

Event poll() {
    bool raw = digitalRead(PIN_BUTTON_PTT);
    uint32_t now = millis();

    if (raw != s_lastRaw) {
        s_lastRaw = raw;
        s_lastChangeMs = now;
    }

    Event ev = Event::None;
    if ((now - s_lastChangeMs) >= BTN_DEBOUNCE_MS && raw != s_stable) {
        s_stable = raw;
        if (s_stable == LOW) {
            // pressed
            s_pressStartMs = now;
            s_longFired = false;
            ev = Event::Pressed;
        } else {
            // released
            ev = Event::Released;
        }
    }

    if (s_stable == LOW && !s_longFired &&
        (now - s_pressStartMs) >= BTN_LONGPRESS_MS) {
        s_longFired = true;
        ev = Event::LongPress;
    }

    return ev;
}

bool isHeld() {
    return s_stable == LOW;
}

} // namespace
