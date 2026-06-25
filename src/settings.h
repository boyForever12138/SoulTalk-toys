#pragma once
#include <Arduino.h>

struct DeviceSettings {
  // WiFi
  String wifiSsid;
  String wifiPass;
  // SoulTalk server
  String host;
  uint16_t port;
  bool tls;
  // Device-side credentials (filled after first register)
  String deviceToken;
  int32_t personaId;    // last known persona id (-1 = none)
  String websocketUrl;  // Dynamic WebSocket URL from server
};

namespace settings {
void begin();
bool load(DeviceSettings &out);
void save(const DeviceSettings &s);
void saveDeviceToken(const String &token);
void savePersonaId(int32_t id);
void saveWebsocketUrl(const String &url);
void clearDeviceBinding();
void clearWifiOnly();
void clearWifiAndToken();
bool hasWifi();
String deviceId();  // MAC-based stable ID
}  // namespace settings
