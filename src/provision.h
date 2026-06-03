#pragma once
#include <Arduino.h>
#include "settings.h"

namespace provision {
    // Blocking: spins SoftAP + captive portal until user submits valid form,
    // then saves to NVS and returns. Uses display/serial for status.
    void runPortal();
    String apSsid();
}
