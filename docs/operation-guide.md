# SoulTalk 硬件设备配套使用文档

> 适用于 [SoulTalk](https://github.com/boyForever12138/SoulTalk) 后端 +
> [SoulTalk-toys](https://github.com/boyForever12138/SoulTalk-toys) ESP32-S3 硬件
> 端的端到端联机使用。
>
> 默认服务端域名：`https://soultalk.kunpenglingjing.cn`

---

## 1. 系统总览

```
┌──────────┐  WiFi   ┌─────────────────────────────────────┐
│ ESP32-S3 │────────▶│ https://soultalk.kunpenglingjing.cn │
│  + INMP441│ HTTPS  │  ├─ /api/devices/register           │
│  + MAX98357│       │  ├─ /api/devices/me                 │
│  + OLED    │       │  ├─ /api/devices/me/persona         │
│  + 按键    │       │  └─ /api/devices/voice  (WSS)       │
└──────────┘         │                                      │
                     │  浏览器 ─▶ /devices/pair (React)     │
                     │            └─ POST /api/devices/pair │
                     └─────────────────────────────────────┘
```

- 设备开机自连家庭 WiFi → 调用 `register` 拿到一份**随机 6 位配对码** → OLED 显示。
- 用户在浏览器登录 SoulTalk 账号，访问 `https://soultalk.kunpenglingjing.cn/devices/pair`，输入码 + 选默认人格，完成绑定。
- 设备轮询 `/me`，绑定成功后立即建立 WebSocket 语音通道；按住按钮即可对话，松开收到 ASR 转写、LLM 回复与 TTS 语音流。

---

## 2. 硬件清单与接线

| 模块 | 引脚 | ESP32-S3 GPIO | 备注 |
|---|---|---|---|
| MAX98357A | BCLK / LRC / DIN | 16 / 17 / 15 | I2S0；Vin 建议 5V 提升音量 |
| INMP441 | SCK / WS / SD | 5 / 4 / 6 | I2S1；L/R→GND 选左声道；VDD=3.3V |
| SSD1306 OLED 0.91" | SCL / SDA | 2 / 42 | I2C；3.3V |
| 按键 | 一脚→GND，另一脚→7 | 7 | 内部上拉 |
| GND | 全部共地 | — | — |

避雷：N16R8 板的 GPIO **33–37** 给 Octal PSRAM 占用，不要使用；19/20 是 USB；26–32 是 SPI flash；0/45/46 是 strapping。当前选脚都安全。

---

## 3. 后端部署

### 3.1 拉取 + 构建

```bash
git clone git@github.com:boyForever12138/SoulTalk.git
cd SoulTalk
```

环境变量（`backend/.env` 增量）：

```bash
# 已有的字段（OPENAI_API_KEY、SESSION_SECRET 等）保持不变

# === TTS（设备端语音回复必需） ===
SPEECH_TTS_PROVIDER=openai           # 或 volcengine
SPEECH_TTS_API_KEY=                  # 留空时回落到 OPENAI_API_KEY
SPEECH_TTS_BASE_URL=                 # 留空时回落到 OPENAI_BASE_URL
SPEECH_TTS_MODEL=tts-1
SPEECH_TTS_VOICE=alloy               # nova/echo/onyx 等可选
SPEECH_TTS_FORMAT=wav                # wav | mp3 | opus
SPEECH_TTS_TIMEOUT_SECONDS=45

# === 火山引擎 TTS（如选 volcengine） ===
SPEECH_TTS_VOLCENGINE_APP_ID=
SPEECH_TTS_VOLCENGINE_ACCESS_TOKEN=
SPEECH_TTS_VOLCENGINE_VOICE_TYPE=BV700_streaming
```

服务器需安装 `ffmpeg`（已被现有 ASR 复用，TTS 输出转 16 kHz mono PCM 也依赖它）。

### 3.2 数据库迁移

```bash
cd backend
alembic upgrade head        # 创建 devices 表（migration: 20260602_0017）
```

dev 模式（`APP_ENV=development`）下 `Base.metadata.create_all` 也会自动建表；生产必须走 alembic。

### 3.3 启动

```bash
# 生产
./start-prod.sh
./start-worker.sh

# 开发
cd backend && uvicorn app.main:app --reload --port 8000
cd frontend && npm ci && npm run dev
```

### 3.4 后端新增/修改文件清单

```
backend/app/auth.py                    # +device token helpers
backend/app/config.py                  # +speech_tts_* 字段
backend/app/llm_client.py              # +synthesize_speech()
backend/app/main.py                    # +devices router
backend/app/models.py                  # +Device 模型
backend/app/api/devices.py             # 新文件：register/me/pair/persona/WS
backend/alembic/versions/20260602_0017_devices.py  # 新文件
frontend/src/App.jsx                   # +/devices/pair 路由
frontend/src/pages/DevicePairPage.jsx  # 新文件：配对页
```

---

## 4. 设备端固件烧录

### 4.1 工具链

- VSCode + PlatformIO 扩展；或 CLI：`pip install platformio`
- USB-C 数据线（识别板载 USB CDC）

### 4.2 编译 / 烧录

```bash
git clone git@github.com:boyForever12138/SoulTalk-toys.git
cd SoulTalk-toys
pio run                # 编译
pio run -t upload      # 烧录
pio device monitor     # 串口 @ 115200 查看日志
```

### 4.3 默认配置

`src/config.h` 默认指向生产域名：

```c
#define DEFAULT_HOST "soultalk.kunpenglingjing.cn"
#define DEFAULT_PORT 443
#define DEFAULT_TLS  true
```

捕获门户里可改成局域网 IP（开发联调用）。

---

## 5. 端到端使用流程

### 5.1 首次开机

1. 上电，OLED 显示 `Setup AP` + `SoulTalk-XXXX`。
2. 手机/电脑连 `SoulTalk-XXXX` 热点（开放无密码）；多数系统会弹出 captive portal，否则浏览器访问 `http://192.168.4.1`。
3. 表单填写：
   - **WiFi SSID / 密码**
   - **Server Host**：`soultalk.kunpenglingjing.cn`（默认）
   - **Port**：`443`（默认）
   - **TLS**：`Yes`（默认）
4. 点 `Save & Reboot`。

### 5.2 联网 + 取配对码

1. 设备重启，OLED 显示 `WiFi/WS...`。
2. WiFi 联通后立即向后端发 `POST /api/devices/register` 拿一个 **随机 6 位配对码**，OLED 切到：

   ```
   Pair code:
   K4T9XR
   ```

3. 配对码 10 分钟过期；过期时只需断电重启，固件会**重新申请**新码（每次冷启动都刷新）。

### 5.3 在浏览器完成绑定

1. 在已登录 SoulTalk 的浏览器打开

   ```
   https://soultalk.kunpenglingjing.cn/devices/pair
   ```

2. 输入 OLED 上的 6 位码，下拉选择默认人格（可跳过）。
3. 点 `绑定设备`，提示绑定成功 + 显示设备 ID。

### 5.4 进入语音对话

1. 设备每 5 秒轮询 `/api/devices/me`；绑定后立即开 `WSS /api/devices/voice?token=…`。
2. OLED 切到 `Ready` 即可使用：
   - **按住按钮**：OLED 显示 `REC *`，PCM 16kHz/16bit 单声道音频流通过 WS 上传。
   - **松开按钮**：发送 `{type:"end"}`，OLED 显示 `Thinking...`。
   - 服务端 ASR → 人格 LLM → TTS，回传二进制 PCM；OLED 切 `Playing >`，喇叭播放。
   - 收到 `{type:"end_of_response"}` 后回到 `Ready`。

### 5.5 切换人格

任选其一：

- 重新打开 `/devices/pair`，用同一账号 + 当前码（10 分钟内）+ 新人格再次绑定。
- 后续可通过 OLED 菜单实现（v0 暂未做）。

### 5.6 重置 / 重新配网

长按 PTT 按钮 5 秒：清空 NVS（WiFi、token、persona），重启进入 SoftAP captive portal。

---

## 6. 协议规范

### 6.1 HTTP 端点

```
# 设备端调用
POST /api/devices/register                    [no auth]
  body: {device_id: str, name?: str}
  resp: {device_token, pair_code?, pair_code_expires_at?, paired}

GET  /api/devices/me                          [Bearer device_token]
  resp: {device_id, paired, user_id?, persona_id?, persona_name?,
         available_personas:[{id,name,description}]}

POST /api/devices/me/persona                  [Bearer device_token]
  body: {persona_id: int}

# 浏览器调用
POST /api/devices/pair                        [cookie auth]
  body: {pair_code: "ABC123", persona_id?: int}

GET  /api/devices/pair                        [cookie auth]
  resp: 内置 HTML 表单（fallback；正式入口为前端 /devices/pair）
```

### 6.2 语音 WebSocket（`WSS /api/devices/voice?token=<device_token>`）

| 方向 | 类型 | 内容 |
|---|---|---|
| C→S | text | `{"type":"start","persona_id"?:int}` |
| C→S | binary | s16le 16kHz mono PCM 帧（建议 20ms = 640B） |
| C→S | text | `{"type":"end"}` 提交一次发言 |
| C→S | text | `{"type":"set_persona","persona_id":int}` |
| C→S | text | `{"type":"ping"}` |
| S→C | text | `{"type":"ready","persona_id":?}` |
| S→C | text | `{"type":"transcript","text":"..."}` |
| S→C | text | `{"type":"reply_text","text":"..."}` |
| S→C | binary | s16le 16kHz mono PCM 合成语音 |
| S→C | text | `{"type":"end_of_response"}` |
| S→C | text | `{"type":"error","stage"?:"asr"\|"chat"\|"tts","message":"..."}` |
| S→C | text | `{"type":"persona_switched","persona_id":int,"persona_name":str}` |

### 6.3 鉴权约定

- **Cookie**：浏览器侧仍用 `soultalk_session` HttpOnly cookie（HMAC-SHA256，TTL 7天），未做改动。
- **设备 Bearer**：`device_token` 用 HMAC 签发，TTL 1 年；任何 `register` 调用都会轮换 token，并在未绑定时同时轮换 pair_code。
- WS 鉴权通过 query 参数 `?token=`（部分浏览器/库不允许在 WS 升级握手携带自定义头）。

---

## 7. 常见问题排错

| 现象 | 排查 |
|---|---|
| OLED 卡 `WiFi/WS...` | WiFi 路由器是否反 2.4G 通过；TLS 设备端使用 `setInsecure()`，CDN 证书需可达；改 `DEFAULT_TLS=false` 可在内网调试 |
| OLED 一直显示 `Pair code` | 浏览器是否已登录；输的码是否就是当前 OLED 上的；过 10 分钟拔电重启刷新 |
| 绑定成功但没声音 | 服务器 `.env` 是否配 `SPEECH_TTS_*`；`ffmpeg --version` 是否能跑；查 `speech_tts_failed` 日志 |
| 录音很轻 | `audio_in.cpp` 中 `>> 11` 改 `>> 9` 加大增益 |
| 喇叭声音小 | MAX98357A Vin 接 5V/VBUS |
| 烧录失败 | OLED SCL 在 GPIO 2（strapping）；改用 GPIO 8 |
| 401 / 403 报错 | 设备 token 过期或被服务端清除 → 长按 PTT 重置重新配对 |

---

## 8. 仓库与代码索引

### SoulTalk（后端）
- 设备 API：[`backend/app/api/devices.py`](../backend/app/api/devices.py)
- 设备模型：[`backend/app/models.py`](../backend/app/models.py)（`Device`）
- 数据库迁移：[`backend/alembic/versions/20260602_0017_devices.py`](../backend/alembic/versions/20260602_0017_devices.py)
- 设备 token：[`backend/app/auth.py`](../backend/app/auth.py)（`create_device_token` / `parse_device_token`）
- TTS：[`backend/app/llm_client.py`](../backend/app/llm_client.py)（`synthesize_speech`）
- 配对页：[`frontend/src/pages/DevicePairPage.jsx`](../frontend/src/pages/DevicePairPage.jsx)

### SoulTalk-toys（固件）
- 状态机：[`src/main.cpp`](../src/main.cpp)
- HTTP API client：[`src/api_client.{h,cpp}`](../src/api_client.h)
- Voice WebSocket：[`src/ws_client.{h,cpp}`](../src/ws_client.h)
- I2S 录音：[`src/audio_in.{h,cpp}`](../src/audio_in.h)（INMP441）
- I2S 播放：[`src/audio_out.{h,cpp}`](../src/audio_out.h)（MAX98357A）
- 配网：[`src/provision.{h,cpp}`](../src/provision.h)
- NVS：[`src/settings.{h,cpp}`](../src/settings.h)
- 显示：[`src/display.{h,cpp}`](../src/display.h)
- 按键：[`src/button.{h,cpp}`](../src/button.h)

---

## 9. 安全 / 上线注意

1. 原型默认 TLS 但 `WiFiClientSecure.setInsecure()` 跳过证书校验。生产应嵌入 root CA 校验。
2. `SESSION_SECRET` 必须设置为长随机串（≥ 32 字节），否则后端启动校验会失败。
3. 设备 token 失效后没有刷新机制；每次重新 register 即得到新 token，原 token 自然失效。
4. 不要把生产 `OPENAI_API_KEY` 放在固件中——固件**没有**直接调 LLM 的能力，所有 LLM/TTS 调用都在服务端进行。
5. 未来要做：设备列表管理（`GET /api/devices`）、解绑（`DELETE /api/devices/{id}`）、用量配额、TTS 流式分段。
