# PaperSat 🛰️

**PaperSat** is a fully self-contained satellite tracking application for the **M5Paper S3** e-ink tablet. It provides real-time orbital predictions, an intuitive polar sky plot, and on-device configuration using modern AMSAT GP data — all without requiring a phone, laptop, or constant internet connection after initial setup.

Designed for amateur radio operators (hams), visual satellite observers, educators, and space enthusiasts who need reliable portable tracking.

## ✨ Key Features

### 📡 Modern GP Data Support
- Downloads and parses the official **AMSAT daily-bulletin.json**
- Extracts both satellite list and TLE data directly from the JSON (no separate Celestrak calls required)
- Future-proof for modern GP data formats

### 🌌 Advanced Polar Plot
- Large, high-contrast polar view centered on your location
- Concentric elevation rings + cardinal azimuth lines
- Live satellite position (shown only when above horizon)
- Direction-of-travel arrow (predicts position ~45 seconds ahead)
- Smart pass trajectory (current pass or next upcoming pass)

### ⏱️ Precise Pass Predictions
- Hybrid SGP4 + custom AOS/LOS refinement for accurate timing
- Displays next 3 passes with:
  - AOS → LOS times (UTC with seconds)
  - Maximum elevation
- Robust even on eccentric orbits (e.g. RS-44)

### 📍 On-Device Configuration
- **6-character Maidenhead grid** input (or 4-character)
- Manual latitude/longitude entry
- **WiFi geolocation** — one-tap "Auto Location via WiFi" sets your QTH using a 6-character Maidenhead grid derived from your public IP
- WiFi setup via built-in **WiFiManager** captive portal (cleanly returns to main screen)
- Manual UTC time/date setting

### 📴 Offline-First & Resilient
- Complete GP data cached in **LittleFS**
- Works fully offline after first successful download
- Smart 24-hour refresh logic when WiFi is available
- "Update GP" button forces immediate refresh of AMSAT data

### 🔋 E-Ink & Power Optimized
- Adaptive refresh rate (15s when satellite is visible, 60s otherwise)
- Selective screen updates to minimize ghosting
- Battery percentage display
- Clean "GP Data MM/DD/YYYY HH:MM UTC" timestamp

## 🛠 Hardware Requirements

- **M5Paper S3** (M5Stack M5Paper with ESP32-S3)
- 960×540 grayscale e-ink display with capacitive touch
- WiFi capability
- microSD card (recommended for reliability)

## 📦 Required Libraries

Install via Arduino IDE Library Manager:

| Library                          | Purpose                              | Notes                                      |
|----------------------------------|--------------------------------------|--------------------------------------------|
| **M5Unified**                    | Core M5Paper S3 + e-ink + touch      | Latest version (≥ 0.2.5)                   |
| **WiFiManager**                  | Captive portal for WiFi setup        | By tzapu                                   |
| **SparkFun SGP4 Arduino Library**| SGP4 orbital propagation             | Use the official SparkFun version          |
| **ArduinoJson**                  | Parse AMSAT GP daily-bulletin.json   | By Benoit Blanchon (latest)                |

**Board Support**: M5Stack board package (includes ESP32-S3 support for M5PaperS3).

## 🚀 Getting Started

1. Install the required libraries and board support in Arduino IDE.
2. Select **Board: M5Stack → M5PaperS3**.
3. Upload `PaperSat.ino`.
4. On first boot, the device will attempt to download the latest AMSAT GP data when WiFi is available.
5. Use the **Setup** menu to configure your location (Maidenhead grid or WiFi geolocation recommended).
6. Use **Select Sat** to choose a satellite from the curated AMSAT list.

## 📖 Usage Tips

- **WiFi Geolocation**: From the Setup menu, tap "Auto Location via WiFi" to automatically set your position using a 6-character Maidenhead grid.
- **Forcing Updates**: Use the "Update GP" button on the satellite selection screen to force a fresh download of AMSAT data.
- **Offline Operation**: After the first successful GP download, PaperSat works completely offline using the cached data.
- **Satellite Switching**: Selecting a new satellite automatically loads its TLE from the GP data and begins tracking immediately.

## 🤝 Credits

- **AMSAT** for providing the excellent daily GP bulletin
- **SparkFun** for the SGP4 library
- Built with **M5Unified** / **M5GFX** by M5Stack

---

**PaperSat** brings professional-grade satellite tracking to a compact, sunlight-readable e-ink device while staying true to the offline-first, self-contained philosophy valued by the amateur radio community.

Clear skies and good passes! 73 de N8HM
