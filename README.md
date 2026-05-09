# PaperSat

**PaperSat** is a satellite tracking application for the **M5Paper S3** e-ink device. It displays real-time satellite position on a polar sky plot, shows upcoming passes, and allows full on-device configuration.

## Features

- **Sky Plot** with azimuth lines and current satellite position (only shown when above the horizon)
- **Next 3 Passes** with accurate UTC AOS/LOS times and maximum elevation
- **Dynamic Refresh Rate**:
  - Updates every **15 seconds** when the satellite is visible
  - Updates every **60 seconds** when the satellite is below the horizon (reduces e-ink wear)
- **Full Screen Refresh** on every update for reliable e-ink rendering
- **On-Device Configuration**:
  - Enter observer location using **Maidenhead grid square**
  - Enter **Latitude / Longitude** manually
  - Select from popular satellites or enter any **custom NORAD ID**
  - WiFi configuration via captive portal
- Fetches TLE data from the **AMSAT** `nasabare.txt` source
- Battery percentage and status display
- Clean, minimal interface optimized for e-ink

## Hardware Requirements

- **M5Paper S3** (or compatible M5Stack device running M5Unified)
- Arduino IDE with the following libraries installed:
  - `M5Unified`
  - `M5GFX`
  - `WiFiManager`
  - `Preferences`
  - `HTTPClient` (built-in)
  - `Sgp4` (SparkFun SGP4 Arduino Library)

## Installation

1. Install the required libraries via the Arduino Library Manager.
2. Clone or download this repository.
3. Open `PaperSat.ino` in the Arduino IDE.
4. Select your board (**M5PaperS3** or equivalent) and upload the sketch.
5. On first boot, use the **Setup** menu to configure WiFi and your location.

## Usage

### Main Screen
- Displays current satellite position on the sky plot (when above horizon)
- Shows the next 3 passes with AOS/LOS times and max elevation
- Battery level and last TLE update time are shown at the bottom

### Select Satellite
- Choose from a curated list of popular satellites commonly found in the AMSAT TLE file
- Or select **Custom NORAD ID** to track any satellite available in the AMSAT TLE file.

### Setup Menu
- **Enter Maidenhead Grid** — Quick location entry
- **Enter Lat / Lon** — Precise coordinate entry
- **Custom NORAD ID** — Track any satellite by catalog number
- **WiFi Configuration** — Opens a captive portal to enter WiFi credentials

## Configuration

PaperSat stores settings (location and selected satellite) in non-volatile memory using the `Preferences` library. Settings persist across reboots.

## Credits

- **Author**: Paul Stoetzer, N8HM
- Built with the excellent [M5Unified](https://github.com/m5stack/M5Unified) library
- Orbital calculations powered by the [SGP4](https://github.com/SparkFun/SparkFun_SGP4_Arduino_Library) library
- TLE data sourced from [AMSAT](https://www.amsat.org/)

## License

This project is released under the **MIT License**.

---

**Note**: This project is optimized for the M5Paper S3 e-ink display. Performance and appearance may vary on other devices.
