// SoulTalk-toys - ESP32-S3 PTT prototype with device-pairing flow
//
// Boot states:
//   Boot -> Provision (no WiFi config) -> reboots
//   Boot -> Connecting (WiFi STA)
//      -> Pairing (no token, token rejected, or token unpaired) ; OLED shows
//         6-char code, device polls /api/devices/me until paired
//      -> WS Connect -> Idle
//
// Runtime:
//   Idle -> Recording (PTT down) -> Waiting (PTT up, send {type:end})
//   Waiting -> Playing (binary frames) -> Idle ({type:end_of_response})
//   Setup button long-press -> clear WiFi only + reboot to provisioning

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

// Per-frame buffers
int16_t s_pcmBuf[AUDIO_FRAME_SAMPLES];
int16_t s_playBuf[AUDIO_FRAME_SAMPLES * 4];

uint32_t s_lastPairPollMs = 0;
uint32_t s_stateStartedMs = 0;
uint32_t s_lastAudioRxMs = 0;
uint32_t s_recordStartedMs = 0;
uint32_t s_recordEndedMs = 0;
uint32_t s_firstAckAudioRxMs = 0;
uint32_t s_firstRealAudioRxMs = 0;
uint32_t s_audioRxBytes = 0;
bool s_responseEnded = false;
bool s_receivingAckAudio = false;
bool s_ackAudioDraining = false;
bool s_metricsPending = false;

void sendStatusNow();
void handleCommand(const JsonDocument &doc);
void resetTurnMetrics();
void sendTurnMetrics(const char *event);
void rebootToWifiProvisioning();
void delayWithSetupButton(uint32_t durationMs);

const char *buttonEventName(button::Event ev) {
  switch (ev) {
    case button::Event::Pressed:
      return "Pressed";
    case button::Event::Released:
      return "Released";
    case button::Event::LongPress:
      return "LongPress";
    case button::Event::None:
    default:
      return "?";
  }
}

void setState(AppState s) {
  s_state = s;
  s_stateStartedMs = millis();
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
  sendStatusNow();
}

bool connectWifi() {
  Serial.printf("[wifi] connecting to %s\n", s_cfg.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(s_cfg.wifiSsid.c_str(), s_cfg.wifiPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
    if (button::pollSetup() == button::Event::LongPress) {
      rebootToWifiProvisioning();
    }
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED)
    return false;
  Serial.printf("[wifi] OK ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  return true;
}

bool isRejectedDeviceToken(int statusCode) {
  return statusCode == 401 || statusCode == 403 || statusCode == 404 ||
         statusCode == 410;
}

void rebootToWifiProvisioning() {
  Serial.println("[setup] long press -> wifi reprovision");
  display_ui::setLine(0, "WiFi Setup");
  display_ui::setLine(1, "Rebooting...");
  display_ui::render();
  Serial.flush();
  settings::clearWifiOnly();
  delay(250);
  ESP.restart();
}

void delayWithSetupButton(uint32_t durationMs) {
  uint32_t start = millis();
  while ((millis() - start) < durationMs) {
    if (button::pollSetup() == button::Event::LongPress) {
      rebootToWifiProvisioning();
    }
    delay(20);
  }
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
    sendStatusNow();
  } else if (!strcmp(type, "command")) {
    handleCommand(doc);
  } else if (!strcmp(type, "listening")) {
    if (s_state == AppState::Recording) {
      display_ui::setLine(1, "Listening");
      display_ui::render();
    }
  } else if (!strcmp(type, "transcript")) {
    display_ui::setLine(1, String("> ") + (const char *)(doc["text"] | ""));
    display_ui::render();
  } else if (!strcmp(type, "reply_text")) {
    s_receivingAckAudio = false;
    s_ackAudioDraining = false;
    // Could show response preview
  } else if (!strcmp(type, "ack_audio_start")) {
    s_receivingAckAudio = true;
    s_ackAudioDraining = false;
    s_responseEnded = false;
    if (s_state == AppState::Waiting || s_state == AppState::Idle) {
      audio_out::reset();
      setState(AppState::Playing);
    }
    display_ui::setLine(1, "Thinking...");
    display_ui::render();
  } else if (!strcmp(type, "ack_audio_end")) {
    s_receivingAckAudio = false;
    s_ackAudioDraining = true;
    audio_out::finish();
  } else if (!strcmp(type, "end_of_response")) {
    s_receivingAckAudio = false;
    s_ackAudioDraining = false;
    s_responseEnded = true;
    s_metricsPending = true;
    audio_out::finish();
    if (audio_out::isDrained()) {
      audio_out::mute();
      sendTurnMetrics("response_done");
      s_metricsPending = false;
      resetTurnMetrics();
      setState(AppState::Idle);
    } else {
      setState(AppState::Playing);
    }
  } else if (!strcmp(type, "error")) {
    Serial.printf("[ws] error: %s\n", (const char *)(doc["message"] | "?"));
    audio_out::reset();
    sendTurnMetrics("response_error");
    resetTurnMetrics();
    s_receivingAckAudio = false;
    s_ackAudioDraining = false;
    setState(AppState::Idle);
  } else if (!strcmp(type, "persona_switched")) {
    int pid = doc["persona_id"] | -1;
    if (pid >= 0) {
      s_cfg.personaId = pid;
      settings::savePersonaId(pid);
      sendStatusNow();
    }
  }
}

void onWsBinary(const uint8_t *data, size_t len) {
  if (s_state == AppState::Waiting || s_state == AppState::Idle)
    setState(AppState::Playing);
  s_audioRxBytes += len;
  uint32_t now = millis();
  if (s_receivingAckAudio) {
    if (s_firstAckAudioRxMs == 0)
      s_firstAckAudioRxMs = now;
  } else {
    s_ackAudioDraining = false;
    if (s_firstRealAudioRxMs == 0)
      s_firstRealAudioRxMs = now;
  }
  size_t samples = len / 2;
  if (samples > sizeof(s_playBuf) / sizeof(s_playBuf[0]))
    samples = sizeof(s_playBuf) / sizeof(s_playBuf[0]);
  memcpy(s_playBuf, data, samples * 2);
  size_t queued = audio_out::enqueue(s_playBuf, samples);
  s_lastAudioRxMs = now;
  if (queued < samples) {
    Serial.printf("[audio] output queue accepted %u/%u samples\n",
                  (unsigned)queued, (unsigned)samples);
  }
}

const char *appStateName(AppState s) {
  switch (s) {
    case AppState::Boot:
      return "boot";
    case AppState::Connecting:
      return "connecting";
    case AppState::Pairing:
      return "pairing";
    case AppState::Idle:
      return "idle";
    case AppState::Recording:
      return "recording";
    case AppState::Waiting:
      return "waiting";
    case AppState::Playing:
      return "playing";
  }
  return "unknown";
}

void sendStatusNow() {
  if (!ws_client::isConnected())
    return;
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  ws_client::sendStatus(appStateName(s_state), s_cfg.personaId, rssi, millis());
}

void resetTurnMetrics() {
  s_recordStartedMs = 0;
  s_recordEndedMs = 0;
  s_firstAckAudioRxMs = 0;
  s_firstRealAudioRxMs = 0;
  s_audioRxBytes = 0;
  s_metricsPending = false;
}

void sendTurnMetrics(const char *event) {
  if (!ws_client::isConnected())
    return;
  uint32_t recordMs = 0;
  if (s_recordStartedMs > 0 && s_recordEndedMs >= s_recordStartedMs) {
    recordMs = s_recordEndedMs - s_recordStartedMs;
  }
  int32_t ackMs = -1;
  if (s_recordEndedMs > 0 && s_firstAckAudioRxMs >= s_recordEndedMs) {
    ackMs = (int32_t)(s_firstAckAudioRxMs - s_recordEndedMs);
  }
  int32_t realMs = -1;
  if (s_recordEndedMs > 0 && s_firstRealAudioRxMs >= s_recordEndedMs) {
    realMs = (int32_t)(s_firstRealAudioRxMs - s_recordEndedMs);
  }
  ws_client::sendDeviceMetrics(event, recordMs, ackMs, realMs, s_audioRxBytes);
}

void enqueueTestTone(uint32_t durationMs) {
  durationMs = constrain(durationMs, (uint32_t)100, (uint32_t)3000);
  audio_out::reset();
  uint32_t remaining = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
  uint32_t phase = 0;
  uint32_t halfPeriod = AUDIO_SAMPLE_RATE / (880 * 2);
  if (halfPeriod == 0)
    halfPeriod = 1;

  while (remaining > 0) {
    size_t n = remaining > AUDIO_FRAME_SAMPLES ? AUDIO_FRAME_SAMPLES : remaining;
    for (size_t i = 0; i < n; ++i) {
      bool high = ((phase / halfPeriod) % 2) == 0;
      s_playBuf[i] = high ? 9000 : -9000;
      phase++;
    }
    audio_out::enqueue(s_playBuf, n);
    remaining -= n;
  }
  s_responseEnded = true;
  audio_out::finish();
  s_lastAudioRxMs = millis();
  setState(AppState::Playing);
}

void handleCommand(const JsonDocument &doc) {
  int commandId = doc["command_id"] | -1;
  const char *name = doc["name"] | "";
  JsonObjectConst payload = doc["payload"].as<JsonObjectConst>();
  if (commandId < 0 || !name || !strlen(name)) {
    if (commandId >= 0)
      ws_client::sendCommandAck(commandId, false, "invalid command");
    return;
  }

  if (!strcmp(name, "set_persona")) {
    int personaId = payload["persona_id"] | -1;
    if (personaId < 0) {
      ws_client::sendCommandAck(commandId, false, "missing persona_id");
      return;
    }
    s_cfg.personaId = personaId;
    settings::savePersonaId(personaId);
    display_ui::setLine(0, "SoulTalk Toy");
    display_ui::setLine(1, "Persona synced");
    display_ui::render();
    ws_client::sendCommandAck(commandId, true, "persona synced");
    sendStatusNow();
    return;
  }

  if (!strcmp(name, "display_text")) {
    String line1 = String((const char *)(payload["line1"] | "SoulTalk"));
    String line2 = String((const char *)(payload["line2"] | ""));
    display_ui::setLine(0, line1.substring(0, 24));
    display_ui::setLine(1, line2.substring(0, 24));
    display_ui::render();
    ws_client::sendCommandAck(commandId, true, "displayed");
    return;
  }

  if (!strcmp(name, "test_sound")) {
    if (s_state == AppState::Recording || s_state == AppState::Waiting) {
      ws_client::sendCommandAck(commandId, false, "device busy");
      return;
    }
    uint32_t durationMs = payload["duration_ms"] | 600;
    enqueueTestTone(durationMs);
    ws_client::sendCommandAck(commandId, true, "test sound queued");
    sendStatusNow();
    return;
  }

  if (!strcmp(name, "reboot")) {
    uint32_t delayMs = payload["delay_ms"] | 800;
    ws_client::sendCommandAck(commandId, true, "rebooting");
    display_ui::setLine(0, "SoulTalk Toy");
    display_ui::setLine(1, "Rebooting...");
    display_ui::render();
    delay(constrain(delayMs, (uint32_t)100, (uint32_t)10000));
    ESP.restart();
    return;
  }

  if (!strcmp(name, "unbind")) {
    ws_client::sendCommandAck(commandId, true, "unbinding");
    display_ui::setLine(0, "SoulTalk Toy");
    display_ui::setLine(1, "Unbinding...");
    display_ui::render();
    settings::clearDeviceBinding();
    delay(300);
    ESP.restart();
    return;
  }

  ws_client::sendCommandAck(commandId, false, "unsupported command");
}

void onWsStatus(bool connected) {
  Serial.printf("[ws] %s\n", connected ? "connected" : "disconnected");
  if (connected) {
    if (s_state == AppState::Connecting)
      setState(AppState::Idle);
  } else {
    audio_out::reset();
    s_responseEnded = false;
    s_receivingAckAudio = false;
    s_ackAudioDraining = false;
    resetTurnMetrics();
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
    Serial.println("[init] WiFi failed, switching to provisioning portal");
    Serial.flush();
    display_ui::setLine(1, "WiFi failed");
    display_ui::render();
    delay(1200);
    settings::clearWifiOnly();
    provision::runPortal();  // never returns (reboots after save)
  }
  Serial.println("[init] WiFi connected");
  Serial.flush();

  // Probe pair status first; if already paired, skip pair-code display
  Serial.println("[init] Checking pair status...");
  Serial.flush();
  setState(AppState::Pairing);
  bool alreadyPaired = false;
  bool shouldRegister = s_cfg.deviceToken.length() == 0;
  if (s_cfg.deviceToken.length()) {
    Serial.println("[init] Has token, polling /me...");
    Serial.flush();
    api_client::MeResult me = api_client::getMe();
    Serial.printf("[init] /me response: ok=%d status=%d paired=%d err=%s\n",
                  me.ok, me.statusCode, me.paired, me.error.c_str());
    Serial.flush();
    if (me.ok) {
      alreadyPaired = me.paired;
      shouldRegister = !me.paired;
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
    } else if (isRejectedDeviceToken(me.statusCode)) {
      Serial.println("[init] Stored token rejected by server, will register");
      Serial.flush();
      shouldRegister = true;
    } else {
      Serial.println("[init] /me failed without token rejection; preserving token");
      Serial.flush();
      display_ui::setLine(1, "Server fail");
      display_ui::render();
      delayWithSetupButton(5000);
      ESP.restart();
    }
  } else {
    Serial.println("[init] No token, will register");
    Serial.flush();
  }

  if (shouldRegister) {
    Serial.println("[init] Not paired, registering...");
    Serial.flush();
    if (!refreshRegistration()) {
      Serial.println("[init] Register failed, restarting in 3s");
      Serial.flush();
      display_ui::setLine(1, "Register fail");
      display_ui::render();
      delayWithSetupButton(3000);
      ESP.restart();
    }
    Serial.println("[init] Registered, polling for pair...");
    Serial.flush();
    while (!pollPaired()) {
      Serial.println("[init] Still waiting for pair...");
      Serial.flush();
      delayWithSetupButton(PAIR_POLL_INTERVAL_MS);
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
  String wsPath = API_PATH_WS_VOICE;

  if (s_cfg.websocketUrl.length() > 0) {
    if (parseWebsocketUrl(s_cfg.websocketUrl, wsHost, wsPort, wsTls, wsPath)) {
      Serial.printf(
          "[init] Using dynamic WebSocket URL: host=%s, port=%d, tls=%d, path=%s\n",
          wsHost.c_str(), wsPort, wsTls, wsPath.c_str());
      Serial.flush();
    } else {
      Serial.println(
          "[init] Failed to parse WebSocket URL, using fallback config");
      Serial.flush();
      wsHost = s_cfg.host;
      wsPort = s_cfg.port;
      wsTls = s_cfg.tls;
      wsPath = API_PATH_WS_VOICE;
    }
  } else {
    Serial.println("[init] No dynamic WebSocket URL, using fallback config");
    Serial.flush();
  }

  ws_client::begin(wsHost, wsPort, wsTls, wsPath, s_cfg.deviceToken);
  Serial.println("[init] WebSocket init complete, entering loop");
  Serial.flush();
}

void loop() {
  static uint32_t s_lastLoopLogMs = 0;
  static uint32_t s_lastStatusMs = 0;
  static uint32_t s_loopCount = 0;
  s_loopCount++;

  // Log every 5 seconds to confirm loop is running
  if (millis() - s_lastLoopLogMs > 5000) {
    Serial.printf("[loop] running, count=%u state=%d ws=%d\n", s_loopCount,
                  (int)s_state, ws_client::isConnected());
    if (s_state == AppState::Waiting || s_state == AppState::Playing) {
      audio_out::logStats();
    }
    Serial.flush();
    s_lastLoopLogMs = millis();
  }

  ws_client::loop();

  if (ws_client::isConnected() &&
      (millis() - s_lastStatusMs) > DEVICE_STATUS_INTERVAL_MS) {
    sendStatusNow();
    s_lastStatusMs = millis();
  }

  button::Event setupEv = button::pollSetup();
  button::Event ev = button::poll();

  // Surface button events on the OLED for user feedback
  if (setupEv != button::Event::None) {
    display_ui::setLine(1, (s_state == AppState::Waiting ||
                            s_state == AppState::Playing ||
                            s_state == AppState::Recording)
                               ? "Busy"
                               : String("SETUP: ") + buttonEventName(setupEv));
    display_ui::render();
  }

  if (ev != button::Event::None) {
    display_ui::setLine(1, (s_state == AppState::Waiting ||
                            s_state == AppState::Playing)
                               ? "Busy"
                               : String("PTT: ") + buttonEventName(ev));
    display_ui::render();
  }

  // Only allow the dedicated Setup button to re-provision WiFi.
  // Device binding is preserved; unbind is a separate server command.
  if (setupEv == button::Event::LongPress && s_state == AppState::Idle) {
    rebootToWifiProvisioning();
  }

  switch (s_state) {
    case AppState::Idle:
      if (ev == button::Event::Pressed && ws_client::isConnected()) {
        Serial.println("[ptt] pressed, starting recording");
        Serial.flush();
        audio_out::reset();
        resetTurnMetrics();
        s_recordStartedMs = millis();
        s_responseEnded = false;
        s_receivingAckAudio = false;
        s_ackAudioDraining = false;
        ws_client::sendStart(s_cfg.personaId);
        setState(AppState::Recording);
      }
      break;

    case AppState::Recording:
      streamOneFrame();
      if (ev == button::Event::Released || !button::isHeld()) {
        Serial.println("[ptt] released, sending end");
        Serial.flush();
        s_recordEndedMs = millis();
        ws_client::sendEnd();
        setState(AppState::Waiting);
      }
      break;

    case AppState::Waiting:
      if ((millis() - s_stateStartedMs) > AUDIO_WAITING_TIMEOUT_MS) {
        Serial.println("[audio] waiting timeout -> idle");
        audio_out::reset();
        s_responseEnded = false;
        sendTurnMetrics("waiting_timeout");
        resetTurnMetrics();
        setState(AppState::Idle);
      }
      break;
    case AppState::Playing:
      if (s_ackAudioDraining && !s_responseEnded && audio_out::isDrained()) {
        audio_out::reset();
        s_ackAudioDraining = false;
        setState(AppState::Waiting);
      } else if (s_responseEnded && audio_out::isDrained()) {
        audio_out::mute();
        if (s_metricsPending) {
          sendTurnMetrics("response_done");
          s_metricsPending = false;
        }
        resetTurnMetrics();
        setState(AppState::Idle);
      } else if (!s_responseEnded && audio_out::isDrained() &&
                 (millis() - s_lastAudioRxMs) > AUDIO_PLAYBACK_IDLE_TIMEOUT_MS) {
        Serial.println("[audio] playback idle timeout -> idle");
        sendTurnMetrics("playback_idle_timeout");
        resetTurnMetrics();
        setState(AppState::Idle);
      } else if ((millis() - s_stateStartedMs) > AUDIO_WAITING_TIMEOUT_MS) {
        Serial.println("[audio] playback hard timeout -> idle");
        audio_out::reset();
        s_responseEnded = false;
        sendTurnMetrics("playback_hard_timeout");
        resetTurnMetrics();
        setState(AppState::Idle);
      }
      break;

    case AppState::Connecting:
    case AppState::Pairing:
    case AppState::Boot:
      break;
  }

  delay(1);
}
