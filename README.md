# SoulTalk-toys

ESP32-S3 (N16R8) push-to-talk hardware companion for the SoulTalk service.
Holds a button to talk; audio is streamed to the SoulTalk server, which runs
Doubao end-to-end realtime speech and streams synthesized voice back to the
device.

## Hardware

- Board: ESP32-S3 DevKitC-1 N16R8 (16MB flash + 8MB Octal PSRAM)
- Mic: INMP441 (I2S)
- Speaker amp: MAX98357A (I2S)
- Display: 0.91" SSD1306 OLED (I2C, 128x32)
- Buttons: tactile switch (PTT), tactile switch (Setup/WiFi)

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
| PTT button | one leg | GPIO 7 |
| PTT button | other leg | GND |
| Setup/WiFi button | one leg | GPIO 8 |
| Setup/WiFi button | other leg | GND |

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
2. **WiFi connect.** As soon as STA is up, a bound device first checks
   `GET <host>/api/devices/me` with its stored bearer device token. If the
   token is valid, it keeps the existing account/persona binding and goes
   straight to voice mode. A new or server-rejected device registers and
   displays a freshly issued **6-character pair code** on the OLED.
3. **Pairing on the web.** Open
   `https://soultalk.kunpenglingjing.cn/devices/pair`
   on any browser while logged in to your SoulTalk account. Enter the code
   shown on the device, optionally pick a default persona, and submit.
4. **Voice WebSocket.** Once paired, the device opens
   `WSS <host>/api/devices/voice?token=<device_token>`.
5. **Push-to-talk.**
   - Hold the PTT button: device sends `{type:"start"}` then streams 16-bit LE
     16kHz mono PCM frames.
   - Release: device sends `{type:"end"}`. Server proxies the turn to Doubao
     realtime dialogue and streams binary PCM back, which the device plays
     through MAX98357A.
   - Server emits `{type:"transcript"}`, `{type:"reply_text"}`,
     `{type:"end_of_response"}` as control messages.
6. **Re-provision WiFi.** Long-press the dedicated Setup/WiFi button for 5 s
   to clear only the saved WiFi credentials and re-enter the captive portal.
   The PTT button only controls voice recording. The device token, persona,
   and WebSocket URL are preserved, so changing WiFi or using a phone hotspot
   does not require rebinding the device.

## Switching persona

Three options:

- During pairing on the web form (default).
- Change the default persona from the SoulTalk device management page.
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

The SoulTalk server must include the device endpoints and Doubao realtime
dialogue gateway. Configure `DOUBAO_S2S_*` in `soultalk-server/api/.env`.
The device still sends and receives 16 kHz mono PCM; Volcengine credentials
stay only on the server.

## Known caveats

- GPIO 2 is a strapping pin; if flashing fails with the OLED powered, move
  SCL to a different GPIO (avoid 33-37 / 19-20 / 26-32 / 0 / 45 / 46).
- MAX98357A is louder on 5V Vin than 3.3V.
- INMP441 sample shift is `>> 11` in [src/audio_in.cpp](src/audio_in.cpp);
  tune (8..14) if levels are too soft or clip.
- Prototype runs unencrypted (HTTP/WS) by default. For production, terminate
  TLS at a reverse proxy and toggle TLS in the captive portal.
