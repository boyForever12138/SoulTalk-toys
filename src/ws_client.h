#pragma once
#include <Arduino.h>

#include <functional>

namespace ws_client {
using TextHandler = std::function<void(const String &json)>;
using BinaryHandler = std::function<void(const uint8_t *data, size_t len)>;
using StatusHandler = std::function<void(bool connected)>;

void begin(const String &host, uint16_t port, bool tls,
           const String &deviceToken);
void loop();
bool isConnected();

void onText(TextHandler h);
void onBinary(BinaryHandler h);
void onStatus(StatusHandler h);

bool sendBinary(const uint8_t *data, size_t len);

// Protocol helpers
bool sendStart(int personaId /* -1 means use device default */);
bool sendEnd();
bool sendPing();
bool sendSetPersona(int personaId);
}  // namespace ws_client
