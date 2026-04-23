# Hujou3 (هجوع) 🎙️✨


![IoT Architecture](https://img.shields.io/badge/Architecture-Distributed_IoT_Hub-00979D?style=for-the-badge&logo=arduino)
![Security](https://img.shields.io/badge/Security-OWASP_Top_10_Hardened-brightgreen?style=for-the-badge&logo=pre-commit)
![Vibe Coded](https://img.shields.io/badge/Development-AI_Vibe_Coded-FFD43B?style=for-the-badge&logo=python)
![Connectivity](https://img.shields.io/badge/Connectivity-HTTPS_SSL_Secure-red?style=for-the-badge&logo=google-cloud)

---

## 📌 Overview

**Hujou3** (هجوع — meaning *deep, tranquil sleep* in Arabic) is a **distributed ambient intelligence system** that automatically manipulates room acoustics and lighting to create ideal comfort environments — for sleeping, waking up, or simply relaxing.

The system is built as a **multi-node distributed architecture**, where each node handles a distinct concern (sensing, processing, actuation) and all nodes coordinate over a secured local network. Critically, the system is designed to operate **with or without voice control**:

| Mode | How It Works |
|:---|:---|
| 🎤 **Voice Mode** | The ESP32 edge node captures audio, streams it to a Python NLP engine for Arabic/English speech recognition, then dispatches commands over HTTP to the Arduino actuator hub. |
| 🖥️ **Manual Mode (Serial)** | Full interactive serial menu on both nodes — browse sounds, switch modes, control lights, adjust settings — no microphone needed. |
| 📺 **OLED UI Mode** | Two physical buttons on the Arduino hub drive a full graphical menu on a 128×64 OLED display — completely standalone, no PC or voice required. |
| 🌐 **HTTP API Mode** | Any device on the local network can `POST` JSON commands to the hub — enables integration with custom apps, scripts, or home automation. |
| ⏰ **Scheduled Mode** | The Python bridge triggers sleep/wake modes automatically at configured times (e.g., 9 PM sleep, 7 AM wake) — fully autonomous. |

> **Key Insight:** Voice is one input channel among many. The distributed architecture ensures the system remains fully functional even if the microphone, the Python bridge, or WiFi are unavailable.

---

## 🏛️ System Architecture

Hujou3 employs a **3-tier distributed architecture** that strictly separates sensing, intelligence, and actuation:

```
┌──────────────────────────────────────────────────────────────────┐
│                    DISTRIBUTED SYSTEM TOPOLOGY                   │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────────┐    921600 baud     ┌──────────────────────┐ │
│  │   ESP32 Node     │ ◄──── Serial ───► │   Python NLP Bridge  │ │
│  │   (Edge Sensor)  │   raw I2S audio   │                      │ │
│  └────────┬─────────┘                   └──────────────────────┘ │
│           │                                                      │
│           │  HTTP/JSON (WiFi)                                    │
│           │  Token + Timestamp Auth                              │
│           ▼                                                      │
│  ┌──────────────────────────────────────┐                        │
│  │       Arduino R4 WiFi Hub            │                        │
│  │       (Actuator + Controller)        │                        │
│  │                                      │                        │
│  │   ┌──────────┐  ┌────────────────┐   │   ┌────────────────┐  │
│  │   │ DFPlayer │  │      OLED  │   │   │  Tuya Cloud    │  │
│  │   │ MP3 Audio│  │  Display + UI  │   ├──►│  Smart Bulb    │  │
│  │   └──────────┘  └────────────────┘   │   │  (HTTPS/TLS)   │  │
│  └──────────────────────────────────────┘   └────────────────┘  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### Node Responsibilities

| Node | Role | Key Functions |
|:---|:---|:---|
| **ESP32 (Edge Sensor)** | Data-plane: captures audio, detects voice activity (VAD), streams raw I2S data to Python, forwards parsed commands to Arduino via HTTP | WiFi, mDNS discovery, NTP sync, I2S mic, button toggle |
| **Python Bridge (AI Engine)** | Intelligence-plane: receives raw audio, sends to Google Speech API for Arabic/English recognition, runs NLP command analysis, dispatches serial commands, manages auto-schedule | `speech_recognition`, serial I/O, cron-like scheduler |
| **Arduino R4 WiFi (Hub)** | Control-plane + actuation: runs the master FSM, drives OLED UI, controls DFPlayer audio, negotiates Tuya Cloud API for smart lighting, serves HTTP endpoint for inbound commands | FSM (9 states), HTTP server, HMAC-SHA256, mDNS, NTP |

---

## 🔄 Finite State Machine (FSM)

The Arduino hub is governed by a **9-state deterministic FSM** that serializes all system behavior:

```
                    ┌──────────────┐
            ┌──────►│  ST_GENERAL  │◄──────────────────────────┐
            │       └──────┬───────┘                           │
            │              │                                   │
            │    ┌─────────┼─────────┬─────────┐               │
            │    ▼         ▼         ▼         ▼               │
            │ ┌────────┐┌────────┐┌────────┐┌──────────┐       │
            │ │SOUND   ││MODES  ││SETTINGS││DEEP      │       │
            │ │BROWSER ││MENU   ││        ││SLEEP     │       │
            │ └───┬────┘└───┬────┘└───┬────┘└──────────┘       │
            │     │         │         │                        │
            │     ▼         ▼         ▼                        │
            │ ┌────────┐┌────────┐┌──────────┐                 │
            │ │PLAYING ││SLEEP  ││WIFI      │                 │
            │ │MANUAL  ││ACTIVE ││SETTINGS  │                 │
            │ └────────┘│WAKE   │└──────────┘                 │
            │           │ACTIVE │                              │
            │           └───┬────┘                             │
            └───────────────┘                                  │
                    │                                          │
                    └──────────────────────────────────────────┘
```

**States:**

| State | Description |
|:---|:---|
| `ST_GENERAL` | Idle home state — main menu on OLED and Serial |
| `ST_SOUND_BROWSER` | Browse and select from 7 ambient soundscapes |
| `ST_PLAYING_MANUAL` | Active manual playback with pause/resume/volume |
| `ST_MODES_MENU` | Choose between Sleep Mode and Wake Mode |
| `ST_SLEEP_ACTIVE` | Automated sleep routine — rain sounds, gradual light dimming |
| `ST_WAKE_ACTIVE` | Automated wake routine — bird sounds, gradual light brightening |
| `ST_DEEP_SLEEP` | Ultra-low-power state — OLED off, audio paused |
| `ST_SETTINGS` | System configuration (WiFi, light toggle) |
| `ST_WIFI_SETTINGS` | Connect/disconnect/forget WiFi networks |

---

## 🛠️ Hardware & Tech Stack

| Component | Function |
|:---|:---|
| **ESP32** | I2S audio capture (INMP441), VAD, WiFi, mDNS, NTP, HTTP client |
| **Arduino R4 WiFi** | Master FSM, HTTP server (port 80), mDNS (`noura-hub.local`), Tuya Cloud API, OLED UI |
| **INMP441 Mic** | Digital I2S microphone — 16kHz sample rate, 32-bit depth |
| **DFPlayer Mini + Speaker** | MP3 playback of 7 ambient soundscapes (wind, birds, ocean, rain, river, bells, deep sleep) |
| **SSD1306 OLED (128×64)** | Real-time graphical UI with battery icon, WiFi signal bars, paginated menus |
| **Tuya Smart Bulb** | Cloud-controlled ambient lighting (on/off, white/yellow, brightness 0–100%, color temp) |
| **2× Physical Buttons** | OLED navigation (Next / Enter) — fully usable without voice |

### Software Stack

| Library / Tech | Purpose |
|:---|:---|
| `ArduinoJson` | JSON serialization for all HTTP payloads |
| `Crypto + SHA256` | HMAC-SHA256 request signing (Tuya Cloud API) |
| `NTPClient` | Epoch time synchronization for replay attack protection |
| `ArduinoMDNS` | Zero-config networking (`noura-hub.local`) |
| `Adafruit SSD1306 + GFX` | OLED display driver and graphics primitives |
| `DFRobotDFPlayerMini` | DFPlayer MP3 module control |
| `speech_recognition` (Python) | Google Speech API integration for Arabic + English |
| `pyserial` (Python) | 921600-baud serial bridge to ESP32 |

---

## 🌐 Networking Layer

Inter-node communication uses a hardened local network protocol:

* **Transport**: `HTTP/1.1 POST` over local WiFi
* **Payload**: JSON-encoded commands (`{"command":"SLEEP_START","token":"...","time":1700000000}`)
* **Discovery**: Zero-config via mDNS — ESP32 resolves `noura-hub.local` dynamically with periodic re-resolution (every 30s)
* **Time Sync**: Both nodes sync to NTP (`pool.ntp.org` / `time.google.com`) to maintain coherent epoch timestamps
* **Cloud Bridge**: Tuya lighting commands use HTTPS/TLS to `openapi.tuyaeu.com` with HMAC-SHA256 signed requests

---

## 🛡️ Security Layer 
Security is embedded in the communication protocol, not bolted on:

### 1. High-Entropy Token Authentication
Every inbound HTTP request must include a shared secret (`HUJOU3_AUTH_TOKEN`). Requests with missing or mismatched tokens receive **`401 Unauthorized`** and are immediately terminated.

### 2. Replay Attack Protection
Each JSON payload includes a live NTP timestamp. The Arduino hub compares the payload time against its own NTP-synced clock:
- If `|Hub Time − Payload Time| > 5 seconds` → **`408 Request Timeout`** — the packet is rejected
- Captured/replayed packets become useless within seconds

### 3. Application Hardening
- **Method Filtering**: Only `POST` is accepted. `GET`, `PUT`, `DELETE`, and scanner probes receive **`405 Method Not Allowed`**
- **Secret Isolation**: All credentials (WiFi, Tuya API keys, auth tokens) are stored in `secrets.h` and excluded via `.gitignore`
- **Input Validation**: JSON body and Content-Length are validated before processing


---

## 🎤 Voice Commands (Arabic + English)

The NLP engine supports **bilingual command recognition** with fuzzy keyword matching:

| Category | Example Commands |
|:---|:---|
| **Sounds** | "شغل مطر" / "play rain" / "طيور" / "ocean" |
| **Light On** | "ولع اللمبه" / "turn on light" |
| **Light Off** | "اطفي اللمبه" / "light off" |
| **Light Color** | "لمبه بيضاء" / "yellow light" |
| **Sleep Mode** | "ابي انام" / "sleep" |
| **Wake Mode** | "صحيني" / "wake" |
| **Stop Mode** | "اوقف الوضع" / "stop mode" |
| **Stop Sound** | "اوقف الصوت" / "stop sound" |

---

*Hujou3 — a distributed ambient intelligence system, engineered to work seamlessly with voice, buttons, serial, HTTP, or fully autonomous scheduling. Technology that dissolves into silence.*
