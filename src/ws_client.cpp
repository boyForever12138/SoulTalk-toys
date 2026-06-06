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
      Serial.println("[ws] Connected to server");
      s_connected = true;
      if (s_onStatus)
        s_onStatus(true);
      break;
    case WStype_DISCONNECTED:
      Serial.println("[ws] Disconnected from server");
      s_connected = false;
      if (s_onStatus)
        s_onStatus(false);
      break;
    case WStype_TEXT: {
      String msg = String((const char *)payload, length);
      Serial.printf("[ws] Received text: %s\n", msg.c_str());
      if (s_onText)
        s_onText(msg);
      break;
    }
    case WStype_BIN:
      Serial.printf("[ws] Received binary: %d bytes\n", length);
      if (s_onBinary)
        s_onBinary(payload, length);
      break;
    case WStype_ERROR:
      Serial.printf("[ws] Error: %s\n",
                    length > 0 ? (char *)payload : "unknown");
      break;
    case WStype_PING:
      Serial.println("[ws] Ping received");
      break;
    case WStype_PONG:
      Serial.println("[ws] Pong received");
      break;
    case WStype_FRAGMENT:
      Serial.println("[ws] Fragment received");
      break;
    case WStype_FRAGMENT_TEXT_START:
      Serial.println("[ws] Fragment text start");
      break;
    case WStype_FRAGMENT_BIN_START:
      Serial.println("[ws] Fragment bin start");
      break;
    case WStype_FRAGMENT_FIN:
      Serial.println("[ws] Fragment fin");
      break;
    default:
      Serial.printf("[ws] Unknown event type: %d\n", type);
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

bool isIpv4Literal(const String &host) {
  if (host.length() == 0)
    return false;
  int dots = 0;
  for (size_t i = 0; i < host.length(); ++i) {
    char c = host.charAt(i);
    if (c == '.') {
      dots++;
      continue;
    }
    if (c < '0' || c > '9')
      return false;
  }
  return dots == 3;
}

String pathWithDeviceToken(const String &path, const String &deviceToken) {
  String out = path.length() > 0 ? path : String(API_PATH_WS_VOICE);
  if (!out.startsWith("/"))
    out = "/" + out;
  if (out.indexOf("token=") >= 0)
    return out;
  out += (out.indexOf('?') >= 0 ? "&" : "?");
  out += "token=" + deviceToken;
  return out;
}

String redactTokenForLog(const String &path) {
  int tokenIdx = path.indexOf("token=");
  if (tokenIdx < 0)
    return path;
  int valueStart = tokenIdx + 6;
  int valueEnd = path.indexOf('&', valueStart);
  if (valueEnd < 0)
    valueEnd = path.length();
  return path.substring(0, valueStart) + "<redacted>" + path.substring(valueEnd);
}
}  // namespace

namespace ws_client {

void begin(const String &host, uint16_t port, bool tls, const String &path,
           const String &deviceToken) {
  String wsHost = host.length() > 0 ? host : String(WS_HOST);
  uint16_t wsPort = port > 0 ? port : WS_PORT;
  bool wsTls = tls;
  String connectIp = "";
  String sniHost = wsHost;
  String requestPath = pathWithDeviceToken(path, deviceToken);

  if (wsTls && isIpv4Literal(wsHost)) {
    connectIp = wsHost;
    sniHost = WS_HOST;
  } else if (wsTls && wsHost == String(WS_HOST) &&
             String(WS_CONNECT_IP).length() > 0) {
    connectIp = WS_CONNECT_IP;
  }

  String logPath = redactTokenForLog(requestPath);
  Serial.printf("[ws] Connecting to %s:%d%s (TLS: %s",
                sniHost.c_str(), wsPort, logPath.c_str(),
                wsTls ? "yes" : "no");
  if (connectIp.length() > 0) {
    Serial.printf(", via IP: %s", connectIp.c_str());
  }
  Serial.println(")");

  if (wsTls) {
    s_ws.beginSSL(sniHost.c_str(), wsPort, requestPath.c_str(), "");
    if (connectIp.length() > 0) {
      s_ws.setConnectIP(connectIp.c_str());
    }

    String origin = "https://" + sniHost;
    s_ws.setExtraHeaders(("Origin: " + origin).c_str());
    Serial.printf("[ws] Set Origin header: %s\n", origin.c_str());
  } else {
    s_ws.begin(wsHost.c_str(), wsPort, requestPath.c_str());
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
