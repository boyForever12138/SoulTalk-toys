#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "api_client.h"
#include "config.h"

namespace {
String s_host;
uint16_t s_port = 80;
bool s_tls = false;
String s_token;

String baseUrl() {
  String url = s_tls ? "https://" : "http://";
  url += s_host;
  url += ":";
  url += String(s_port);
  return url;
}

bool beginRequest(HTTPClient &http, const String &path) {
  String url = baseUrl() + path;
  if (s_tls) {
    // Prototype: skip cert validation. Replace with rootCA in production.
    static WiFiClientSecure secure;
    secure.setInsecure();
    return http.begin(secure, url);
  }
  return http.begin(url);
}
}  // namespace

namespace api_client {

void configure(const String &host, uint16_t port, bool tls) {
  s_host = host;
  s_port = port;
  s_tls = tls;
}
void setDeviceToken(const String &token) {
  s_token = token;
}

RegisterResult registerDevice(const String &deviceId, const String &name) {
  RegisterResult out{false, "", "", "", false, ""};
  HTTPClient http;
  if (!beginRequest(http, API_PATH_REGISTER)) {
    out.error = "begin failed";
    return out;
  }
  http.addHeader("Content-Type", "application/json");
  JsonDocument req;
  req["device_id"] = deviceId;
  if (name.length())
    req["name"] = name;
  String body;
  serializeJson(req, body);
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  if (code != 200) {
    out.error = String("HTTP ") + code + ": " + resp;
    return out;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    out.error = "JSON parse failed";
    return out;
  }
  out.ok = true;
  out.deviceToken = String((const char *)(doc["device_token"] | ""));
  out.pairCode = String((const char *)(doc["pair_code"] | ""));
  out.websocketUrl = String((const char *)(doc["websocket_url"] | ""));
  out.paired = doc["paired"] | false;
  return out;
}

MeResult getMe() {
  MeResult out{false, 0, false, -1, "", "", {}, ""};
  HTTPClient http;
  if (!beginRequest(http, API_PATH_ME)) {
    out.error = "begin failed";
    return out;
  }
  http.addHeader("Authorization", "Bearer " + s_token);
  int code = http.GET();
  out.statusCode = code;
  String resp = http.getString();
  http.end();
  if (code != 200) {
    out.error = String("HTTP ") + code;
    return out;
  }
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    out.error = "JSON parse failed";
    return out;
  }
  out.ok = true;
  out.paired = doc["paired"] | false;
  out.personaId = doc["persona_id"] | -1;
  out.personaName = String((const char *)(doc["persona_name"] | ""));
  out.websocketUrl = String((const char *)(doc["websocket_url"] | ""));
  JsonArrayConst arr = doc["available_personas"].as<JsonArrayConst>();
  for (JsonVariantConst v : arr) {
    PersonaInfo pi;
    pi.id = v["id"] | 0;
    pi.name = String((const char *)(v["name"] | ""));
    out.personas.push_back(pi);
  }
  return out;
}

bool setPersona(int personaId, String *errorOut) {
  HTTPClient http;
  if (!beginRequest(http, API_PATH_SET_PERSONA)) {
    if (errorOut)
      *errorOut = "begin failed";
    return false;
  }
  http.addHeader("Authorization", "Bearer " + s_token);
  http.addHeader("Content-Type", "application/json");
  JsonDocument req;
  req["persona_id"] = personaId;
  String body;
  serializeJson(req, body);
  int code = http.POST(body);
  String resp = http.getString();
  http.end();
  if (code != 200) {
    if (errorOut)
      *errorOut = String("HTTP ") + code + ": " + resp;
    return false;
  }
  return true;
}

}  // namespace api_client
