#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "config.h"
#include "ws_client.h"

namespace {
WebSocketsClient s_ws;
bool s_connected = false;

ws_client::TextHandler s_onText;
ws_client::BinaryHandler s_onBinary;
ws_client::StatusHandler s_onStatus;

void onEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      s_connected = true;
      if (s_onStatus)
        s_onStatus(true);
      break;
    case WStype_DISCONNECTED:
      s_connected = false;
      if (s_onStatus)
        s_onStatus(false);
      break;
    case WStype_TEXT:
      if (s_onText)
        s_onText(String((const char *)payload, length));
      break;
    case WStype_BIN:
      if (s_onBinary)
        s_onBinary(payload, length);
      break;
    default:
      break;
  }
}

bool sendJson(const JsonDocument &doc) {
  if (!s_connected)
    return false;
  String s;
  serializeJson(doc, s);
  return s_ws.sendTXT(s);
}
}  // namespace

namespace ws_client {

void begin(const String &host, uint16_t port, bool tls,
           const String &deviceToken) {
  String path = String(API_PATH_WS_VOICE) + "?token=" + deviceToken;
  if (tls) {
    s_ws.beginSSL(host.c_str(), port, path.c_str());
  } else {
    s_ws.begin(host.c_str(), port, path.c_str());
  }
  s_ws.onEvent(onEvent);
  s_ws.setReconnectInterval(2000);
  s_ws.enableHeartbeat(15000, 3000, 2);
}

void loop() {
  s_ws.loop();
}
bool isConnected() {
  return s_connected;
}

void onText(TextHandler h) {
  s_onText = h;
}
void onBinary(BinaryHandler h) {
  s_onBinary = h;
}
void onStatus(StatusHandler h) {
  s_onStatus = h;
}

bool sendBinary(const uint8_t *data, size_t len) {
  if (!s_connected)
    return false;
  return s_ws.sendBIN(data, len);
}

bool sendStart(int personaId) {
  JsonDocument doc;
  doc["type"] = "start";
  if (personaId >= 0)
    doc["persona_id"] = personaId;
  return sendJson(doc);
}

bool sendEnd() {
  if (!s_connected)
    return false;
  return s_ws.sendTXT("{\"type\":\"end\"}");
}

bool sendPing() {
  if (!s_connected)
    return false;
  return s_ws.sendTXT("{\"type\":\"ping\"}");
}

bool sendSetPersona(int personaId) {
  JsonDocument doc;
  doc["type"] = "set_persona";
  doc["persona_id"] = personaId;
  return sendJson(doc);
}

}  // namespace ws_client
