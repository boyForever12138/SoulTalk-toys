#include <Preferences.h>
#include <WiFi.h>

#include "config.h"
#include "settings.h"

namespace {
Preferences prefs;
constexpr const char *NS = "soultalk";
}  // namespace

namespace settings {

void begin() {
  prefs.begin(NS, false);
}

bool load(DeviceSettings &out) {
  out.wifiSsid = prefs.getString("ssid", "");
  out.wifiPass = prefs.getString("pass", "");
  out.host = prefs.getString("host", DEFAULT_HOST);
  out.port = prefs.getUShort("port", DEFAULT_PORT);
  out.tls = prefs.getBool("tls", DEFAULT_TLS);
  out.deviceToken = prefs.getString("dtoken", "");
  out.personaId = prefs.getInt("persona", -1);
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

void clearWifiAndToken() {
  prefs.clear();
}

bool hasWifi() {
  return prefs.getString("ssid", "").length() > 0;
}

String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "esp32s3-%012llx", (unsigned long long)mac);
  return String(buf);
}

}  // namespace settings
