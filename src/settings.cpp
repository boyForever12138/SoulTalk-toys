#include <Preferences.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"

namespace {
Preferences prefs;
constexpr const char *NS = "soultalk";

// Safe getter that checks key existence first to avoid NVS "NOT_FOUND" error
// logs
String safeGetString(const char *key, const String &defaultVal) {
  if (!prefs.isKey(key))
    return defaultVal;
  return prefs.getString(key, defaultVal);
}
}  // namespace

namespace settings {

void begin() {
  prefs.begin(NS, false);
}

bool load(DeviceSettings &out) {
  out.wifiSsid = safeGetString("ssid", "");
  out.wifiPass = safeGetString("pass", "");
  out.host = safeGetString("host", DEFAULT_HOST);
  out.port = prefs.getUShort("port", DEFAULT_PORT);
  out.tls = prefs.getBool("tls", DEFAULT_TLS);
  out.deviceToken = safeGetString("dtoken", "");
  out.personaId = prefs.isKey("persona") ? prefs.getInt("persona", -1) : -1;
  out.websocketUrl = safeGetString("wsUrl", "");

  // Auto-upgrade: if saved config is HTTP, force HTTPS (server requires it)
  if (!out.tls && out.host == DEFAULT_HOST) {
    Serial.println("[settings] Auto-upgrading HTTP to HTTPS");
    out.tls = true;
    out.port = 443;
    // Save upgraded config to NVS
    prefs.putBool("tls", true);
    prefs.putUShort("port", 443);
  }

  return out.wifiSsid.length() > 0;
}

void save(const DeviceSettings &s) {
  prefs.putString("ssid", s.wifiSsid);
  prefs.putString("pass", s.wifiPass);
  prefs.putString("host", s.host);
  prefs.putUShort("port", s.port);
  prefs.putBool("tls", s.tls);
  prefs.putString("dtoken", s.deviceToken);
  prefs.putInt("persona", s.personaId);
}

void saveDeviceToken(const String &token) {
  prefs.putString("dtoken", token);
}
void savePersonaId(int32_t id) {
  prefs.putInt("persona", id);
}
void saveWebsocketUrl(const String &url) {
  prefs.putString("wsUrl", url);
}

void clearWifiAndToken() {
  prefs.clear();
}

bool hasWifi() {
  return prefs.isKey("ssid") && prefs.getString("ssid", "").length() > 0;
}

String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "esp32s3-%012llx", (unsigned long long)mac);
  return String(buf);
}

}  // namespace settings
