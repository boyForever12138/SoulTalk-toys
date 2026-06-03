# SoulTalk-toys

ESP32-S3 (N16R8) push-to-talk hardware companion for the SoulTalk service.
Holds a button to talk; audio is streamed to the SoulTalk server, which runs
ASR -> persona LLM -> TTS and streams synthesized voice back to the device.

## Hardware

- Board: ESP32-S3 DevKitC-1 N16R8 (16MB flash + 8MB Octal PSRAM)
- Mic: INMP441 (I2S)
- Speaker amp: MAX98357A (I2S)
- Display: 0.91" SSD1306 OLED (I2C, 128x32)
- Button: tactile switch (PTT)

### Wiring

| Module | Pin on module | ESP32-S3 GPIO |
|---|---|---|
| MAX98357A | BCLK | 16 |
| MAX98357A | LRC  | 17 |
| MAX98357A | DIN  | 15 |
| MAX98357A | Vin  | 3.3V (5V recommended for louder output) |
| INMP441 | SCK | 5 |
| INMP441 | WS  | 4 |
| INMP441 | SD  | 6 |
| INMP441 | L/R | GND |
| INMP441 | VDD | 3.3V |
| OLED | SCL | 2 |
| OLED | SDA | 42 |
| OLED | VCC | 3.3V |
| Button | one leg | GPIO 7 |
| Button | other leg | GND |

All GNDs tied together.

## Build (PlatformIO)

```
pio run            # compile
pio run -t upload  # flash
pio device monitor # serial @ 115200
```

## End-to-end flow

1. **First boot** opens a SoftAP `SoulTalk-XXXX`. Connect your phone/laptop.
   Captive portal asks for: home WiFi SSID/password and SoulTalk server
   `host:port` (+ TLS toggle). Defaults are
   `soultalk.kunpenglingjing.cn:443` with TLS on, so for the production
   service you can leave them untouched. Save and the device reboots.
2. **WiFi connect.** As soon as STA is up the device calls
   `POST <host>/api/devices/register {device_id}`, stores the long-lived
   bearer device token in NVS, and immediately displays the freshly
   issued **6-character pair code** on the OLED.
3. **Pairing on the web.** Open
   `https://soultalk.kunpenglingjing.cn/devices/pair`
   on any browser while logged in to your SoulTalk account. Enter the code
   shown on the device, optionally pick a default persona, and submit.
4. **Voice WebSocket.** Once paired, the device opens
   `WSS <host>/api/devices/voice?token=<device_token>`.
5. **Push-to-talk.**
   - Hold the button: device sends `{type:"start"}` then streams 16-bit LE
     16kHz mono PCM frames.
   - Release: device sends `{type:"end"}`. Server runs ASR -> LLM -> TTS and
     streams binary PCM back, which the device plays through MAX98357A.
   - Server emits `{type:"transcript"}`, `{type:"reply_text"}`,
     `{type:"end_of_response"}` as control messages.
6. **Re-provision.** Long-press PTT for 5 s to wipe NVS and re-enter the
   captive portal. A fresh pair code is issued on every cold boot until
   the device is bound to an account.

## Switching persona

Three options:

- During pairing on the web form (default).
- Re-open the pair page anytime: rebinding overwrites the persona.
- Programmatically: send `{"type":"set_persona","persona_id":<id>}` over the
  voice WebSocket. (UI for this on the device is left as future work --
  could use button + OLED menu.)

## Module layout

- [src/main.cpp](src/main.cpp) -- state machine
- [src/settings.{h,cpp}](src/settings.h) -- NVS
- [src/provision.{h,cpp}](src/provision.h) -- SoftAP captive portal
- [src/api_client.{h,cpp}](src/api_client.h) -- HTTP register / me / set-persona
- [src/ws_client.{h,cpp}](src/ws_client.h) -- voice WebSocket protocol
- [src/audio_in.{h,cpp}](src/audio_in.h) -- INMP441 (I2S1)
- [src/audio_out.{h,cpp}](src/audio_out.h) -- MAX98357A (I2S0)
- [src/button.{h,cpp}](src/button.h) -- PTT debounce + long-press
- [src/display.{h,cpp}](src/display.h) -- U8g2 OLED status

## Backend dependencies

The SoulTalk server must include the new device endpoints
(`backend/app/api/devices.py`) and the device migration
(`backend/alembic/versions/20260602_0017_devices.py`). Set TTS provider via
`SPEECH_TTS_PROVIDER`/`SPEECH_TTS_API_KEY` in `backend/.env` (mirrors the
existing ASR provider settings). `ffmpeg` must be installed on the server
(used to resample TTS output to 16 kHz mono PCM for the device).

## Known caveats

- GPIO 2 is a strapping pin; if flashing fails with the OLED powered, move
  SCL to a different GPIO (avoid 33-37 / 19-20 / 26-32 / 0 / 45 / 46).
- MAX98357A is louder on 5V Vin than 3.3V.
- INMP441 sample shift is `>> 11` in [src/audio_in.cpp](src/audio_in.cpp);
  tune (8..14) if levels are too soft or clip.
- Prototype runs unencrypted (HTTP/WS) by default. For production, terminate
  TLS at a reverse proxy and toggle TLS in the captive portal.
