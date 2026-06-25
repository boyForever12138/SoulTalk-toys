#pragma once
#include <Arduino.h>

namespace button {
    enum class Event {
        None,
        Pressed,        // edge: just pressed
        Released,       // edge: just released
        LongPress       // held >= BTN_LONGPRESS_MS (fires once while still held)
    };

    void begin();
    Event poll();
    Event pollSetup();
    bool isHeld();
    bool isSetupHeld();
}
