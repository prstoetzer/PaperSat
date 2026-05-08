# PaperSat - M5Paper S3 Satellite Tracker

**A standalone, touch-first satellite tracking application for the M5Paper S3 e-ink tablet.**

Built for amateur radio operators, satellite enthusiasts, and educators. Shows real-time satellite position on a polar sky map, predicts upcoming passes, and supports **any satellite** in the Celestrak database.

## Features

- Full touch UI — no physical buttons required
- On-device setup via WiFi captive portal:
- Maidenhead grid square (preferred) **or** manual latitude/longitude
- Any NORAD catalog number
- Quick-select menu for popular satellites
- Real-time sky radar plot (polar view with current satellite position)
- Next 5–8 pass predictions (AOS/LOS times and max elevation)
- Auto-refreshes every 30 seconds
- Persistent configuration (saved in flash memory)
- Extremely low power — perfect for always-on desk or field use

## Hardware Requirements

- **M5Paper S3** (ESP32-S3 + 4.7" 960×540 e-ink display)
- Optional: MicroSD card (for future logging features)

## Software Requirements (Arduino IDE)

### Board Support
1. Open **File → Preferences**
2. Add the following URL to **Additional Boards Manager URLs**:
`https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json`
3. Go to **Tools → Board → Boards Manager** and install **M5Stack**
4. Select board: **M5PaperS3**

### Required Libraries (via Library Manager)
- **M5Unified** (≥ 0.2.5)
- **M5GFX** (≥ 0.2.7)
- **WiFiManager** by tzapu
- **SparkFun SGP4** (or Hopperpop SGP4 library)

## Installation

1. Clone or download the repository
2. Open PaperSat.ino in the Arduino IDE
3. Select Tools → Board → M5Stack → M5PaperS3
4. Choose the correct USB port
5. Upload the sketch (long-press the power button on the M5Paper S3 if it doesn’t enter download mode automatically)  

## First Boot and Setup

1. After uploading, tap the Setup button on the main screen  
2. Connect your phone or computer to the WiFi network: M5PaperS3-Setup  
3. Open the captive portal in your browser  
4. Enter your 6-character Maidenhead grid square (e.g. FN31pr) or latitude and longitude  
5. Optionally enter any NORAD ID  
6. Save — the device will reconnect to your home WiFi  
7. The display will now auto-update every 30 seconds  

## Author

Paul Stoetzer, N8HM  
Executive Vice President  
AMSAT  

## License

This project is licensed under the MIT License — see the LICENSE file for details.  

## Acknowledgments  

• Celestrak for public TLE data  
• SGP4 algorithm by Dr. T.S. Kelso  
• M5Stack hardware and libraries  
• WiFiManager and Arduino open-source community  

