// SoulTalk-toys - ESP32-S3 PTT prototype with device-pairing flow
//
// Boot states:
//   Boot -> Provision (no WiFi config) -> reboots
//   Boot -> Connecting (WiFi STA)
//      -> Pairing (no token, or token unpaired) ; OLED shows 6-char code,
//         device polls /api/devices/me until paired
//      -> WS Connect -> Idle
//
// Runtime:
//   Idle -> Recording (PTT down) -> Waiting (PTT up, send {type:end})
//   Waiting -> Playing (binary frames) -> Idle ({type:end_of_response})
//   Idle -> wipe NVS + reboot (long-press 5s)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "api_client.h"
#include "audio_in.h"
#include "audio_out.h"
#include "button.h"
#include "config.h"
#include "display.h"
#include "pins.h"
#include "provision.h"
#include "settings.h"
#include "ws_client.h"

namespace {
enum class AppState {
  Boot,
  Connecting,
  Pairing,
  Idle,
  Recording,
  Waiting,
  Playing
};
AppState s_state = AppState::Boot;

DeviceSettings s_cfg;

// Per-frame buffer for recording
int16_t s_pcmBuf[AUDIO_FRAME_SAMPLES];

uint32_t s_lastPairPollMs = 0;
uint32_t s_eorTimeMs = 0;  // timestamp when end_of_response was received

void setState(AppState s) {
  s_state = s;
  switch (s) {
    case AppState::Boot:
      display_ui::setState(display_ui::State::Boot);
      break;
    case AppState::Connecting:
      display_ui::setState(display_ui::State::Connecting);
      break;
    case AppState::Pairing:
      display_ui::setState(display_ui::State::Pairing);
      break;
    case AppState::Idle:
      display_ui::setState(display_ui::State::Idle);
      break;
    case AppState::Recording:
      display_ui::setState(display_ui::State::Recording);
      break;
    case AppState::Waiting:
      display_ui::setState(display_ui::State::Waiting);
      break;
    case AppState::Playing:
      display_ui::setState(display_ui::State::Playing);
      break;
  }
}

bool connectWifi() {
  Serial.printf("[wifi] connecting to %s\n", s_cfg.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(s_cfg.wifiSsid.c_str(), s_cfg.wifiPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED)
    return false;
  Serial.printf("[wifi] OK ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  return true;
}

// Always (re)register on boot when not yet paired so the OLED always shows
// a freshly issued, currently-valid 6-char pair code. The backend rotates
// both device_token and pair_code each call (when device.user_id is null).
bool refreshRegistration() {
  api_client::RegisterResult r =
      api_client::registerDevice(settings::deviceId(), "SoulTalk Toy");
  if (!r.ok) {
    Serial.printf("[register] failed: %s\n", r.error.c_str());
    return false;
  }
  s_cfg.deviceToken = r.deviceToken;
  settings::saveDeviceToken(r.deviceToken);
  api_client::setDeviceToken(r.deviceToken);

  // Store websocket URL if provided
  if (r.websocketUrl.length() > 0) {
    s_cfg.websocketUrl = r.websocketUrl;
    settings::saveWebsocketUrl(r.websocketUrl);
    Serial.printf("[register] Got websocket URL: %s\n", r.websocketUrl.c_str());
  }

  Serial.printf("[register] paired=%d pair_code=%s\n", r.paired,
                r.pairCode.c_str());
  if (!r.paired && r.pairCode.length()) {
    display_ui::setLine(0, "Pair code:");
    display_ui::setLine(1, r.pairCode);
    display_ui::render();
  }
  return true;
}

bool pollPaired() {
  api_client::MeResult me = api_client::getMe();
  if (!me.ok) {
    Serial.printf("[me] err: %s\n", me.error.c_str());
    return false;
  }
  if (me.paired) {
    if (me.personaId >= 0) {
      s_cfg.personaId = me.personaId;
      settings::savePersonaId(me.personaId);
    }
    return true;
  }
  return false;
}

void onWsText(const String &json) {
  Serial.printf("[ws<-] %s\n", json.c_str());
  JsonDocument doc;
  if (deserializeJson(doc, json))
    return;
  const char *type = doc["type"] | "";
  if (!strcmp(type, "ready")) {
    int pid = doc["persona_id"] | -1;
    if (pid >= 0)
      s_cfg.personaId = pid;
    setState(AppState::Idle);
  } else if (!strcmp(type, "transcript")) {
    display_ui::setLine(1, String("> ") + (const char *)(doc["text"] | ""));
    display_ui::render();
  } else if (!strcmp(type, "reply_text")) {
    // Could show response preview
  } else if (!strcmp(type, "end_of_response")) {
    // Record when end_of_response arrived. The actual clear happens after a
    // grace period to ensure all buffered audio has been played.
    s_eorTimeMs = millis();
    setState(AppState::Waiting);
  } else if (!strcmp(type, "error")) {
    Serial.printf("[ws] error: %s\n", (const char *)(doc["message"] | "?"));
    audio_out::clear();
    audio_out::mute();
    setState(AppState::Idle);
  } else if (!strcmp(type, "persona_switched")) {
    int pid = doc["persona_id"] | -1;
    if (pid >= 0) {
      s_cfg.personaId = pid;
      settings::savePersonaId(pid);
    }
  }
}

void onWsBinary(const uint8_t *data, size_t len) {
  if (s_state == AppState::Waiting)
    setState(AppState::Playing);
  audio_out::push(data, len, 2);
}

void onWsStatus(bool connected) {
  Serial.printf("[ws] %s\n", connected ? "connected" : "disconnected");
  if (connected) {
    if (s_state == AppState::Connecting)
      setState(AppState::Idle);
  } else {
    if (s_state != AppState::Connecting)
      setState(AppState::Connecting);
  }
}

void streamOneFrame() {
  size_t got = audio_in::read(s_pcmBuf, AUDIO_FRAME_SAMPLES, 30);
  if (got == 0)
    return;
  ws_client::sendBinary((const uint8_t *)s_pcmBuf, got * sizeof(int16_t));
}

// Parse WebSocket URL (ws://host:port/path or wss://host:port/path)
// Returns true if successfully parsed, false otherwise
bool parseWebsocketUrl(const String &url, String &outHost, uint16_t &outPort,
                       bool &outTls, String &outPath) {
  // Check protocol
  bool isSecure = false;
  int protocolEnd = 0;
  if (url.startsWith("wss://")) {
    isSecure = true;
    protocolEnd = 6;
  } else if (url.startsWith("ws://")) {
    isSecure = false;
    protocolEnd = 5;
  } else {
    return false;
  }

  // Find host:port
  int hostStart = protocolEnd;
  int pathStart = url.indexOf('/', hostStart);
  if (pathStart < 0) {
    pathStart = url.length();
  }

  // Extract host and port
  String hostPort = url.substring(hostStart, pathStart);
  int colonIdx = hostPort.lastIndexOf(':');
  if (colonIdx >= 0) {
    // Has explicit port
    outHost = hostPort.substring(0, colonIdx);
    outPort = hostPort.substring(colonIdx + 1).toInt();
  } else {
    // No explicit port, use protocol defaults
    outHost = hostPort;
    outPort = isSecure ? 443 : 80;
  }

  // Extract path
  if (pathStart < (int)url.length()) {
    outPath = url.substring(pathStart);
  } else {
    outPath = "/";
  }

  outTls = isSecure;
  return true;
}
}  // namespace

void setup() {
  // Initialize serial output
  // With ARDUINO_USB_CDC_ON_BOOT=0: Serial uses UART0 (TX=GPIO43, RX=GPIO44)
  // This works with USB-to-UART bridge chips (CP2102/CH340) commonly on DevKit
  // boards
  Serial.begin(115200);
  delay(500);

  Serial.println("\n[soultalk-toys] boot");
  Serial.flush();

  Serial.println("[init] Starting NVS...");
  Serial.flush();
  settings::begin();
  Serial.println("[init] NVS ready");
  Serial.flush();

  Serial.println("[init] Starting display...");
  Serial.flush();
  display_ui::begin();
  Serial.println("[init] Display ready");
  Serial.flush();

  Serial.println("[init] Checking WiFi config...");
  Serial.flush();
  if (!settings::hasWifi()) {
    Serial.println("[init] No WiFi config found, starting provisioning portal");
    Serial.flush();
    provision::runPortal();  // never returns (reboots)
  }
  Serial.println("[init] WiFi config found, continuing boot");
  Serial.flush();

  settings::load(s_cfg);
  Serial.println("[init] Config loaded");
  Serial.flush();

  api_client::configure(s_cfg.host, s_cfg.port, s_cfg.tls);
  Serial.println("[init] API client configured");
  Serial.flush();

  if (s_cfg.deviceToken.length()) {
    api_client::setDeviceToken(s_cfg.deviceToken);
    Serial.println("[init] Device token set");
    Serial.flush();
  }
  Serial.printf("[cfg] device_id=%s host=%s://%s:%u persona=%d\n",
                settings::deviceId().c_str(), s_cfg.tls ? "https" : "http",
                s_cfg.host.c_str(), s_cfg.port, s_cfg.personaId);
  Serial.flush();

  Serial.println("[init] Starting button...");
  Serial.flush();
  button::begin();
  Serial.println("[init] Button ready");
  Serial.flush();

  Serial.println("[init] Starting audio_out...");
  Serial.flush();
  if (!audio_out::begin()) {
    Serial.println("[audio_out] init FAIL");
    Serial.flush();
  } else {
    Serial.println("[init] audio_out ready");
    Serial.flush();
  }

  Serial.println("[init] Starting audio_in...");
  Serial.flush();
  if (!audio_in::begin()) {
    Serial.println("[audio_in] init FAIL");
    Serial.flush();
  } else {
    Serial.println("[init] audio_in ready");
    Serial.flush();
  }

  Serial.println("[init] Entering Connecting state...");
  Serial.flush();
  setState(AppState::Connecting);

  Serial.println("[init] Connecting WiFi...");
  Serial.flush();
  if (!connectWifi()) {
    Serial.println("[init] WiFi failed, restarting in 2s");
    Serial.flush();
    display_ui::setLine(1, "WiFi failed");
    display_ui::render();
    delay(2000);
    ESP.restart();
  }
  Serial.println("[init] WiFi connected");
  Serial.flush();

  // Probe pair status first; if already paired, skip pair-code display
  Serial.println("[init] Checking pair status...");
  Serial.flush();
  setState(AppState::Pairing);
  bool alreadyPaired = false;
  if (s_cfg.deviceToken.length()) {
    Serial.println("[init] Has token, polling /me...");
    Serial.flush();
    api_client::MeResult me = api_client::getMe();
    Serial.printf("[init] /me response: ok=%d paired=%d\n", me.ok, me.paired);
    Serial.flush();
    alreadyPaired = me.ok && me.paired;
    if (alreadyPaired) {
      if (me.personaId >= 0) {
        s_cfg.personaId = me.personaId;
        settings::savePersonaId(me.personaId);
      } else if (!me.personas.empty()) {
        // No persona selected yet, auto-pick the first available one
        int firstId = me.personas[0].id;
        Serial.printf("[init] No persona set, auto-selecting #%d (%s)\n",
                      firstId, me.personas[0].name.c_str());
        Serial.flush();
        if (api_client::setPersona(firstId)) {
          s_cfg.personaId = firstId;
          settings::savePersonaId(firstId);
        }
      }
      // Store websocket URL for later use
      if (me.websocketUrl.length() > 0) {
        s_cfg.websocketUrl = me.websocketUrl;
        settings::saveWebsocketUrl(me.websocketUrl);
        Serial.printf("[init] Got websocket URL: %s\n",
                      me.websocketUrl.c_str());
        Serial.flush();
      }
    }
  } else {
    Serial.println("[init] No token, will register");
    Serial.flush();
  }

  if (!alreadyPaired) {
    Serial.println("[init] Not paired, registering...");
    Serial.flush();
    if (!refreshRegistration()) {
      Serial.println("[init] Register failed, restarting in 3s");
      Serial.flush();
      display_ui::setLine(1, "Register fail");
      display_ui::render();
      delay(3000);
      ESP.restart();
    }
    Serial.println("[init] Registered, polling for pair...");
    Serial.flush();
    while (!pollPaired()) {
      Serial.println("[init] Still waiting for pair...");
      Serial.flush();
      delay(PAIR_POLL_INTERVAL_MS);
      button::Event ev = button::poll();
      if (ev == button::Event::LongPress) {
        // Long press during pairing: refresh pair code (it may have expired)
        // Do NOT clear WiFi config - user should not have to re-enter WiFi
        Serial.println(
            "[init] Long press during pairing, refreshing pair code");
        Serial.flush();
        if (refreshRegistration()) {
          Serial.println("[init] Pair code refreshed");
          Serial.flush();
        } else {
          Serial.println("[init] Failed to refresh pair code, will retry");
          Serial.flush();
        }
      }
    }
    Serial.println("[pair] paired");
    Serial.flush();
  } else {
    Serial.println("[pair] already paired, skipping code display");
    Serial.flush();
  }

  // Open voice WebSocket
  Serial.println("[init] Connecting WebSocket...");
  Serial.flush();
  setState(AppState::Connecting);
  ws_client::onText(onWsText);
  ws_client::onBinary(onWsBinary);
  ws_client::onStatus(onWsStatus);

  // Use dynamic WebSocket URL if available, otherwise fall back to config
  String wsHost = s_cfg.host;
  uint16_t wsPort = s_cfg.port;
  bool wsTls = s_cfg.tls;

  if (s_cfg.websocketUrl.length() > 0) {
    String path;
    if (parseWebsocketUrl(s_cfg.websocketUrl, wsHost, wsPort, wsTls, path)) {
      Serial.printf(
          "[init] Using dynamic WebSocket URL: %s (host=%s, port=%d, tls=%d)\n",
          s_cfg.websocketUrl.c_str(), wsHost.c_str(), wsPort, wsTls);
      Serial.flush();
    } else {
      Serial.println(
          "[init] Failed to parse WebSocket URL, using fallback config");
      Serial.flush();
      wsHost = s_cfg.host;
      wsPort = s_cfg.port;
      wsTls = s_cfg.tls;
    }
  } else {
    Serial.println("[init] No dynamic WebSocket URL, using fallback config");
    Serial.flush();
  }

  ws_client::begin(wsHost, wsPort, wsTls, s_cfg.deviceToken);
  Serial.println("[init] WebSocket init complete, entering loop");
  Serial.flush();
}

void loop() {
  static uint32_t s_lastLoopLogMs = 0;
  static uint32_t s_loopCount = 0;
  s_loopCount++;

  // Log every 5 seconds to confirm loop is running
  if (millis() - s_lastLoopLogMs > 5000) {
    Serial.printf("[loop] running, count=%u state=%d ws=%d\n", s_loopCount,
                  (int)s_state, ws_client::isConnected());
    Serial.flush();
    s_lastLoopLogMs = millis();
  }

  ws_client::loop();

  button::Event ev = button::poll();

  // Surface button events on the OLED for user feedback
  if (ev != button::Event::None) {
    const char *evName = ev == button::Event::Pressed     ? "Pressed"
                         : ev == button::Event::Released  ? "Released"
                         : ev == button::Event::LongPress ? "LongPress"
                                                          : "?";
    display_ui::setLine(1, String("BTN: ") + evName);
    display_ui::render();
  }

  // Only allow long press to wipe & reboot when in Idle state
  // This prevents accidental reset during connection/pairing
  if (ev == button::Event::LongPress && s_state == AppState::Idle) {
    Serial.println("[btn] long press -> wipe & reboot");
    Serial.flush();
    settings::clearWifiAndToken();
    ESP.restart();
  }

  switch (s_state) {
    case AppState::Idle:
      if (ev == button::Event::Pressed && ws_client::isConnected()) {
        Serial.println("[ptt] pressed, starting recording");
        Serial.flush();
        // Stop any ongoing playback immediately
        audio_out::stop();
        ws_client::sendStart(s_cfg.personaId);
        setState(AppState::Recording);
      }
      break;

    case AppState::Recording:
      streamOneFrame();
      if (ev == button::Event::Released || !button::isHeld()) {
        Serial.println("[ptt] released, sending end");
        Serial.flush();
        ws_client::sendEnd();
        setState(AppState::Waiting);
      }
      break;

    case AppState::Waiting:
    case AppState::Playing:
      audio_out::update();
      audio_out::printStats();
      // After end_of_response, wait for buffer to drain + grace period before
      // clearing. This prevents cutting off the tail end of audio.
      if (s_state == AppState::Waiting && audio_out::isBufferEmpty()) {
        uint32_t elapsed = millis() - s_eorTimeMs;
        if (elapsed > 500) {  // 500ms grace after buffer empties
          audio_out::clear();
          audio_out::mute();
          setState(AppState::Idle);
        }
      }
      break;

    case AppState::Connecting:
    case AppState::Pairing:
    case AppState::Boot:
      break;
  }

  delay(1);
}
