# SoulTalk Toy Operation Guide

This firmware is an ESP32-S3 push-to-talk client for SoulTalk. The server now
uses Doubao end-to-end realtime speech for device calls.

## Runtime Flow

```text
ESP32-S3 button/mic
  -> WSS /api/devices/voice?token=<device_token>
SoulTalk server
  -> Doubao realtime dialogue
SoulTalk server
  -> binary PCM audio
ESP32-S3 speaker
```

The device does not call Doubao directly and does not store Volcengine
credentials.

## Audio Contract

- Device upload: `s16le`, 16 kHz, mono PCM
- Server download: `s16le`, 16 kHz, mono PCM
- Recommended frame size: 20 ms = 320 samples = 640 bytes

## WebSocket Protocol

| Direction | Type | Payload |
|---|---|---|
| C -> S | text | `{"type":"start","persona_id"?:int}` |
| C -> S | binary | raw PCM frame |
| C -> S | text | `{"type":"end"}` |
| C -> S | text | `{"type":"set_persona","persona_id":int}` |
| C -> S | text | `{"type":"ping"}` |
| S -> C | text | `{"type":"ready","persona_id":?}` |
| S -> C | text | `{"type":"listening"}` |
| S -> C | text | `{"type":"transcript","text":"..."}` |
| S -> C | text | `{"type":"reply_text","text":"..."}` |
| S -> C | binary | raw PCM audio |
| S -> C | text | `{"type":"end_of_response"}` |
| S -> C | text | `{"type":"error","stage"?: "...","message":"..."}` |

## Server Configuration

Configure `soultalk-server/api/.env`:

```env
DOUBAO_S2S_ENABLED=true
DOUBAO_S2S_BASE_URL=wss://openspeech.bytedance.com/api/v3/realtime/dialogue
DOUBAO_S2S_APP_ID=
DOUBAO_S2S_ACCESS_KEY=
DOUBAO_S2S_APP_KEY=PlgvMymc7f3tQnJ6
DOUBAO_S2S_RESOURCE_ID=volc.speech.dialog
DOUBAO_S2S_MODEL=1.2.1.1
DOUBAO_S2S_SPEAKER=zh_female_vv_jupiter_bigtts
DOUBAO_S2S_INPUT_FORMAT=pcm
DOUBAO_S2S_INPUT_SAMPLE_RATE=16000
DOUBAO_S2S_OUTPUT_FORMAT=pcm_s16le
DOUBAO_S2S_OUTPUT_SAMPLE_RATE=16000
# Set this when the public CDN does not proxy WebSocket.
DEVICE_VOICE_WS_URL=wss://<origin-host-or-ip>/api/devices/voice
```

## Build And Flash

```bash
pio run
pio run -t upload
pio device monitor
```

## Device Use

1. First boot opens the provisioning portal.
2. Configure WiFi and SoulTalk server host.
3. The OLED shows a 6-character pair code.
4. Pair the device in the SoulTalk web UI.
5. When OLED shows `Ready`, hold the button to talk.
6. Release the button to submit the turn.
7. The device plays streamed PCM audio and returns to `Ready`.

## Troubleshooting

- `Doubao S2S is not configured`: server `.env` is missing `DOUBAO_S2S_*`.
- Device connects but no audio: confirm Doubao output is `pcm_s16le` 16 kHz.
- WebSocket fails online: bypass CDN for `/api/devices/voice`.
- Pair code keeps changing: the device is not paired yet; every registration
  rotates the unpaired token and code.
