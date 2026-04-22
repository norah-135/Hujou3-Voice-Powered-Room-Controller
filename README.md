# 🌙 Hujou3 (هجوع) | Voice-Powered Ambient Intelligence

<div align="center">
  
![Project Name](https://img.shields.io/badge/Project-Hujou3_Voice_Powered-blueviolet?style=for-the-badge&logo=soundcharts)
![IoT Architecture](https://img.shields.io/badge/Architecture-Distributed_IoT_Hub-00979D?style=for-the-badge&logo=arduino)
![Security](https://img.shields.io/badge/Security-OWASP_Top_10_Hardened-brightgreen?style=for-the-badge&logo=pre-commit)
![Vibe Coded](https://img.shields.io/badge/Development-AI_Vibe_Coded-FFD43B?style=for-the-badge&logo=python)
![Connectivity](https://img.shields.io/badge/Connectivity-HTTPS_SSL_Secure-red?style=for-the-badge&logo=google-cloud)
</div>

## 📌 Overview

**Hujou3** (meaning deep, tranquil sleep in Arabic) is a sophisticated voice-controlled ambient intelligence system. Born from the philosophy that technology should blend invisibly into human solitude, it actively manipulates environmental acoustics and illumination to provide profound comfort. 

Breaking away from traditional development pipelines, Hujou3 is proudly architected through **AI-Assisted Engineering (Vibe Coding)**. This paradigm enabled rapid, highly-secure iterations of complex NLP analysis and distributed systems logic, yielding a robust product in record time.

---

## 🏗️ System Architecture

Hujou3 employs a **Distributed System Architecture** strictly separating the control-plane from the data-plane:

1. **The Brain (ESP32 Node)**: Acts as the intelligent decision-maker and local gateway. It captures human interaction via I2S, processes audio buffers, and formulates actionable directives.
2. **The Actuator Hub (Arduino R4 WiFi)**: Operates as the physical execution layer. Driven by a deterministic **Finite State Machine (FSM)**, it parses directives, manages power states, and directly controls the Tuya lighting ecosystem and DFPlayer audio output.

---

## 🛡️ Security Layer 

Security is not an afterthought in Hujou3; it is woven into the communication protocol.

### 1. High-Entropy Authentication (Shared Secret)
All inbound commands to the Hub are dropped unless validated by a persistent identity mechanism. A cryptographic, **High-Entropy Token** (`HUJOU3_AUTH_TOKEN`) validates every request. Requests lacking this token yield a `401 Unauthorized` and are aggressively terminated.

### 2. Replay Attack Protection (Temporal Validation)
To defend against network sniffing and replay attacks, the system utilizes **Temporal Validation**:
* The ESP32 injects a live **NTP Timestamp** into every JSON payload.
* Upon reception, the Arduino Hub validates that the command was sent within a **5-second window**.
* Stolen packets become entirely useless within seconds, ensuring the system's integrity even on unencrypted local channels.

### 3. Application Hardening
* **Secret Isolation**: All credentials and tokens are sequestered in a `.gitignore` isolated header file (`secrets.h`).
* **Method Enforcement**: The HTTP handler acts as a strict firewall, rejecting any verb other than `POST` with a `405 Method Not Allowed`.

---

## 🌐 Networking & Connectivity

* **Inter-Device Communication**: Utilizes optimized `HTTP/1.1 POST` requests for minimal latency.
* **Payload Structure**: Commands are serialized into lightweight **JSON** formats for efficient parsing.
* **Cloud Integration**: Lighting commands trigger direct secure `HTTPS` calls to the **Tuya Cloud OpenAPI** using `HMAC-SHA256` signatures.
* **Time Synchronization**: Integrated `NTPClient` ensures both nodes stay in sync for security validations.

---

## 🛠️ Hardware & Tech Stack

| Component | Role |
| :--- | :--- |
| **ESP32 Edge Node** | Audio capture (I2S), VAD, & Network Gateway. |
| **Arduino R4 WiFi** | State Machine execution & Hardware actuation. |
| **INMP441 Mic** | High-fidelity digital audio capture. |
| **Tuya Smart Ecosystem** | Cloud-based ambient lighting control. |
| **DFPlayer Mini** | Local environmental soundscape playback. |
="center">
  <i>Hujou3 architecture embodies the pinnacle of secure, embedded ambient IoT systems.</i>
  <br>
  <b>Developed by Nora | IoT Engineering</b>
</div>
