#pragma once

#include <Arduino.h>

// Audio (matches backend WS protocol: 16-bit LE 16kHz mono PCM)
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_BITS 16
#define AUDIO_CHANNELS 1
// 20 ms frame @ 16 kHz / 16-bit / mono = 320 samples = 640 bytes
#define AUDIO_FRAME_SAMPLES 320
#define AUDIO_FRAME_BYTES (AUDIO_FRAME_SAMPLES * 2)

// Provisioning AP
#define PROV_AP_PREFIX "SoulTalk-"
#define PROV_AP_PASSWORD ""  // open AP for prototype

// Default SoulTalk server (override via captive portal)
#define DEFAULT_HOST "soultalk.kunpenglingjing.cn"
#define DEFAULT_PORT 443
#define DEFAULT_TLS true

// WebSocket connection: connect directly to origin IP (CDN doesn't proxy WS),
// but present the domain as TLS SNI and HTTP Host header so nginx routes
// to the correct server block.
#define WS_HOST "soultalk.kunpenglingjing.cn"
#define WS_CONNECT_IP "119.45.31.103"
#define WS_PORT 443
#define WS_TLS true

// SSL certificate fingerprint for soultalk.kunpenglingjing.cn (SHA1)
// Update this when certificate is renewed
#define SOULTALK_SSL_FINGERPRINT \
  "1B:91:89:5E:7D:0C:43:6E:B5:09:0C:19:01:71:22:AF:00:F7:A5:C6"

// HTTP/WebSocket paths on the SoulTalk backend
#define API_PATH_REGISTER "/api/devices/register"
#define API_PATH_ME "/api/devices/me"
#define API_PATH_SET_PERSONA "/api/devices/me/persona"
#define API_PATH_WS_VOICE "/api/devices/voice"

// Pairing poll interval after register
#define PAIR_POLL_INTERVAL_MS 5000

// Button timing
#define BTN_DEBOUNCE_MS 30
#define BTN_LONGPRESS_MS 5000
