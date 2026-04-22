# Hujou3 (هجوع) 🎙️✨


![IoT](https://img.shields.io/badge/IoT-Distributed_System-00979D?style=for-the-badge&logo=arduino)
![AI-Assisted](https://img.shields.io/badge/Engineering-AI_Assisted_Vibe_Coding-FFD43B?style=for-the-badge&logo=openai)
![Security](https://img.shields.io/badge/Security-OWASP_IoT_Ready-brightgreen?style=for-the-badge&logo=shield)
![Status](https://img.shields.io/badge/Phase-Production_Ready-blue?style=for-the-badge)

## 📌 Overview

**Hujou3** (meaning deep, tranquil sleep in Arabic) is a sophisticated voice-controlled ambient intelligence system. Born from the philosophy that technology should blend invisibly into human solitude, it actively manipulates environmental acoustics and illumination to provide profound comfort. 

Breaking away from traditional development pipelines, Hujou3 is proudly architected through **AI-Assisted Engineering (Vibe Coding)**. This paradigm enabled rapid, highly-secure iterations of complex NLP analysis and distributed systems logic, yielding a robust product in record time.

---

## 🛠️ Hardware & Tech Stack

This project orchestrates a heterogeneous array of hardware and advanced software libraries.

| Component | Function / Role |
| :--- | :--- |
| **ESP32 Edge Node** | Audio capture, VAD, High-speed Serial streaming, HTTP payload construction. |
| **Arduino R4 WiFi** | FSM execution, Tuya Cloud negotiation, hardware actuation. |
| **INMP441 Mic** | High-fidelity digital I2S audio capture. |
| **DFPlayer Mini + Speaker** | Local playback of environmental soundscapes (MP3). |
| **SSD1306 OLED** | Real-time diagnostics and local state telemetry. |
| **Tuya Smart Bulb** | Environment ambient lighting. |

**Core SDKs & Libraries**:
* `ArduinoJson` (Serialization/Deserialization)
* `Tuya SDK / OpenAPI` (Cloud Lighting control)
* `NTPClient` (Live Temporal Synchronization)
* `Crypto / SHA256` (HMAC request signing)

---

## 🏛️ System Architecture

Hujou3 employs a **Distributed System Architecture** strictly separating the control-plane from the data-plane:

1. **The Brain (ESP32 Node)**: Acts as the intelligent decision-maker and local gateway. It captures human interaction, bridges the raw acoustic data to the computing engine via a 921600-baud data-link, and ultimately formulates actionable directives.
2. **The Actuator Hub (Arduino R4 WiFi)**: Operates as the physical execution layer. Driven by a deterministic Finite State Machine (FSM), it parses directives, manages power states, and directly controls the Tuya lightning ecosystem and DFPlayer audio output.

---

## 🌐 Networking Layer

Intra-system communication utilizes a hardened, localized network protocol:
* **Protocol**: Restful `HTTP/1.1 POST` requests transmitted over local WiFi.
* **Payload Structure**: Commands are serialized into lightweight **JSON** formats.
* **Discovery**: Relies on Zero-Configuration Networking (mDNS `noura-hub.local`) to bridge the Edge Node and Actuator Hub dynamically.
* **Time Synchronization**: Integrated `NTPClient` continuously syncs system epoch time to prevent clock drift and enforce packet validity.
* **Cloud API Bridge**: Lighting commands trigger direct `POST` calls to the Tuya Cloud OpenAPI using meticulously calculated `HMAC-SHA256` signatures.

---

## 🛡️ Security Layer (OWASP IoT Top 10)

Security is not an afterthought in Hujou3; it is woven into the protocol.

### 1. High-Entropy Authentication (Shared Secret)
All inbound commands to the Arduino Hub are dropped unless validated by a persistent identity mechanism. A cryptographic, **High-Entropy Token** (`HUJOU3_AUTH_TOKEN`) validates identity. If the JSON payload lacks the matching shared secret, the connection yields `401 Unauthorized` and is aggressively terminated.

### 2. Replay Attack Protection (Temporal Validation)
To defend against network sniffing and replay attacks, the system utilizes **Temporal Validation**:
* The ESP32 injects a live NTP `Timestamp` into every JSON payload.
* Upon reception, the Arduino Hub extracts the packet's time and compares it against its own NTP-synchronized clock.
* If `|Hub Time - Payload Time| > 5 seconds`, the command is categorically rejected with a `408 Request Timeout`. Stolen packets become entirely useless within margin of network transit.

### 3. Application Hardening & Method Filtering
Aligning with OWASP best practices for IoT deployment:
* **Secret Isolation**: All credentials, tuples, and secrets are stripped from source code and sequestered in a `.gitignore` isolated `#pragma once` header file (`secrets.h`).
* **Method Enforcement**: The HTTP handler acts as a strict firewall. Any HTTP verb other than `POST` (e.g., standard browser `GET` requests, scanner probing) is instantly slapped with a `405 Method Not Allowed`, slamming the door on reconnaissance attempts.

---

*Hujou3 architecture embodies the pinnacle of secure, embedded ambient IoT systems.*
