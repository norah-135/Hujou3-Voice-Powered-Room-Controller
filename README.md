# Hujou3 (هجوع) 🎙️✨


![Architecture](https://img.shields.io/badge/Architecture-Dual_MCU-00979D?style=for-the-badge) ![AI Engine](https://img.shields.io/badge/AI_Engine-Python_NLP-FFD43B?style=for-the-badge&logo=python&logoColor=blue) ![Security](https://img.shields.io/badge/Security-Hardened_(OWASP)-brightgreen?style=for-the-badge&logo=shield) ![Status](https://img.shields.io/badge/Status-Production_Ready-blue?style=for-the-badge)

**Hujou3 (هجوع)** is a next-generation, voice-powered ambient room ecosystem. 
Designed from the ground up to be seamless, intelligent, and secure, 
it bridges the gap between hardware engineering and generative AI logic. 

Whether you want to sleep to the sound of rain as your lights dim, 
or wake up to birds chirping with warm sunlight — **just say the word**.

---

## 🌟 The Hujou3 Experience

Hujou3 eliminates friction. No apps, no buttons, no complicated phrases. 
It understands **context**.

* **"وضع النوم" (Sleep mode)**: 
  Instantly triggers an immersive rain soundscape, turns off the main lights, 
  and gradually fades the audio volume over 2 hours.

* **"وضع الاستيقاض" (Wake Up mode)**: 
  Welcomes you to the morning with birdsong and gradually turns on the smart lighting.

* **"لمبة" (Light)**: 
  A true smart-toggle. The AI intelligently determines the current state of 
  your Tuya light and flips it without needing you to specify "on" or "off".

* **"مطر" (Rain) / "محيط" (Ocean)**: 
  Direct sound activation with zero latency. 

---

## 🔥 Pro Engineering Highlights

This isn't a simple hobby script; it is a **highly optimized, multi-tier IoT system** 
demonstrating advanced embedded systems design.

### 1. 🧠 Python AI & NLP Engine
Instead of basic keyword matching, the Python engine acts as the "Brain".
* Captures raw PCM audio streamed over a high-speed **921600 baud rate** connection.
* Interfaces with Google Speech Recognition for bilingual transcription.
* Applies **Multi-pass Contextual Logic**: Automatically corrects missing operators 
  (e.g., inferring toggle logic) and handles compound intents.
* Features a **Daemon Auto-Scheduler** that autonomously triggers Deep Sleep at 9:00 PM 
  and Wake Mode at 7:00 AM without human intervention.

### 2. ⚡ Deterministic FSM (Finite State Machine)
The Arduino Hub runs a strict, 9-state deterministic machine.
* **Blind-Spot Elimination**: Prevents "deaf modes" where the system ignores 
  voice commands while sleeping. The hardware actively manages screen wake-locks.
* **Real-Time Physics Battery Simulator**: Calculates system draw dynamically 
  (WiFi load, OLED usage, Audio amp draw).

### 3. 🛡️ Industrial-Grade IoT Security
Secured against intercepts, rogue commands, and replay attacks.
* **Layer 1 (Method Filtering)**: The HTTP Server aggressively drops non-POST 
  requests, preventing port-scanners (`405 Method Not Allowed`).
* **Layer 2 (Secret-Space Auth)**: A heavily guarded `#pragma once` architecture. 
  Every payload carries an encrypted `HUJOU3_AUTH_TOKEN`.
* **Layer 3 (NTP Replay Protection)**: The ESP32 signs payloads with live Unix 
  Epoch timestamps. The Arduino Hub cross-references this with its own NTP client.

### 4. 🌐 Zero-Config Discovery (mDNS)
No hardcoded IP addresses. The ESP32 dynamically locates the hub on the local 
network (`noura-hub.local`).

---

## 🛠️ The Tech Stack

| Component | Technology | Purpose |
| --- | --- | --- |
| **Edge Audio Node** | `ESP32`, `C++`, `I2S` | Captures high-fidelity 16kHz audio. |
| **Brain / NLP** | `Python 3.12` | The AI controller. Transcribes and infers context. |
| **Execution Hub** | `Arduino R4 WiFi` | Runs FSM, controls OLED, drives DFPlayer Mini. |
| **Cloud Bridge** | `Tuya OpenAPI` | Communicates with Tuya using request signing. |

---

## 🚀 Getting Started

### 1. Prerequisites

```bash
# Python Requirements
pip install pyserial SpeechRecognition

# Arduino Libraries
ArduinoJson, DFRobotDFPlayerMini, Adafruit_SSD1306, NTPClient, Crypto, ArduinoMDNS
```

### 2. Setup Sequence

1. **Secure your Keys**: Populate `secrets.h` with your WiFi SSID & Tuya keys.
2. **Flash the Edge Node**: Upload `esp32.ino` to the ESP32.
3. **Flash the Core Hub**: Upload `UI.ino` to the Arduino R4.
4. **Ignite the Brain**: Run `python SecureVER.py` on your host machine.

---

**Built with absolute precision, obsession with security, and an AI-driven vision 
for the future of ambient computing.**

*هجوع — Your environment, mastered.*
