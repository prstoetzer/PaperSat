# PaperSat 🛰️

**PaperSat** is a fully self-contained, professional-grade satellite tracking application built specifically for the **M5Paper S3** e-ink tablet. It delivers real-time orbital predictions, an intuitive polar sky plot with pass trajectories, and on-device configuration — all without requiring a phone, laptop, or constant internet after the initial setup.

Designed for amateur radio operators (hams), visual satellite observers, educators, and space enthusiasts who need reliable tracking in the field or at home.


## ✨ Key Features

### 📡 Complete On-Device Satellite Database
- Downloads and parses the full **AMSAT nasabare.txt** TLE collection (~60+ popular & amateur satellites)
- Paged, touch-optimized selection screen (10 satellites per page) — instantly switch tracking target
- No manual NORAD ID typing required

### 🌌 Advanced Polar Az/El Sky Plot
- Large, high-contrast polar view centered on your location (QTH)
- Concentric elevation rings (horizon → mid → zenith) + 8 cardinal azimuth radials with N/S/E/W labels
- **Live satellite position** shown as a filled square (only when above 0° elevation)
- **Direction-of-travel arrow** — predicts where the satellite will be ~45 seconds ahead
- **Smart pass trajectory**:
  - When visible: draws the full AOS-to-LOS ground track across your sky
  - When below horizon: automatically renders the *next* upcoming pass path
- Clean e-ink friendly rendering with motion vector for intuitive visualization

### ⏱️ Precise Pass Predictions
- Up to 8 future passes computed using hybrid SGP4 + custom AOS/LOS refinement (robust even on eccentric orbits like RS-44)
- Displays the **next 3 passes** with:
  - AOS → LOS times in UTC (with seconds precision)
  - Maximum elevation reached
  - Custom degree symbol (filled circle)
- Smart partial-screen updates to minimize e-ink ghosting

### 📍 Live Tracking & Status
- Real-time **Azimuth** and **Elevation** readout (bottom left)
- Adaptive refresh: 15 s when satellite is visible (El > 0), 60 s otherwise (power efficient)
- Always-visible UTC time + battery percentage
- TLE freshness indicator ("updated X min ago")
- Helpful status messages (downloading, using cache, errors, etc.)

### ⚙️ Fully On-Device Touch Configuration
- **Maidenhead Grid Locator** input (4- or **6-character** precision — perfect for hams)
- Manual **Lat / Lon** entry (decimal degrees)
- **Manual UTC Time/Date** setting (useful offline or when NTP unavailable)
- WiFi setup via built-in **WiFiManager** captive portal (SSID: `M5PaperS3-Setup`)

### 📴 Offline-First & Resilient Design
- Complete `nasabare.txt` cached in **LittleFS** internal flash
- Works 100% offline after first successful TLE download
- Smart 24-hour refresh logic when WiFi is available
- Graceful fallback to cached data if download fails or no internet

### 🔋 E-Ink & Power Optimized
- Selective `fillRect` clears for dynamic areas (pass list, status)
- Full screen refreshes only when changing screens
- Visibility-based polling interval
- Battery voltage monitoring with percentage display

## 🛠 Hardware Requirements

- **M5Paper S3** (M5Stack M5Paper with ESP32-S3)
- Built-in 960×540 e-ink display, capacitive touch, WiFi/BT, and LiPo battery
- (Strongly recommended) Stable WiFi for initial TLE download and occasional updates

## 📦 Required Libraries & Board Support

Install everything via the Arduino IDE **Library Manager** and **Boards Manager**.

### 1. Board Support (M5Paper S3)

1. Open Arduino IDE → **File → Preferences**
2. In **Additional Board Manager URLs**, add this line:
   ```
   https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
   ```
3. Click **OK**, then go to **Tools → Board → Boards Manager**
4. Search for **M5Stack** and install the latest **M5Stack** board package (this includes ESP32-S3 support for M5PaperS3)
5. After installation, select **Board: M5Stack → M5PaperS3** (or equivalent under ESP32 Arduino boards)

### 2. Core Libraries (install via Library Manager)

| Library                          | Purpose                              | Install Notes / Recommended Version      |
|----------------------------------|--------------------------------------|------------------------------------------|
| **M5Unified**                    | Core M5Paper S3 + e-ink + touch      | Latest (≥ 0.2.5). Also pulls in M5GFX   |
| **WiFiManager**                  | Captive portal for easy WiFi setup   | By tzapu (latest)                        |
| **SparkFun SGP4 Arduino Library**| SGP4 propagator + pass prediction    | By SparkFun (this is the Hopperpop port with `nextpass`/`initpredpoint` API used by PaperSat) |

**Important Library Notes**:
- Use the **SparkFun SGP4** library (not other random Sgp4 ports). It provides the exact API (`sat.nextpass(&p, 40, false, 0.0)`, `initpredpoint`, `findsat(unix)`, `satEl`/`satAz`) that PaperSat relies on.
- Older or alternative SGP4 libraries often have different method signatures (`predict()`, different `passinfo` struct, etc.) and will cause compile errors.
- M5Unified is actively maintained — always use the newest version.

## 🚀 Compilation & Upload Guide

### Step-by-Step

1. **Install Arduino IDE** (2.x recommended) from arduino.cc
2. Add the M5Stack board manager URL and install the board package (see above)
3. Install the three required libraries via **Sketch → Include Library → Manage Libraries**
4. Download or clone this repository and open `PaperSat.ino`
5. Select **Board: M5PaperS3**
6. Select the correct USB serial port
7. **(Optional but often required)** Put the M5Paper S3 into download mode:
   - Connect via USB
   - Long-press the power button until the back LED blinks red
8. Click **Upload** (the right-arrow button)

The sketch is ~860 lines and compiles cleanly on a modern Arduino IDE + up-to-date libraries.

### Common Compilation Errors & Fixes

| Error Message                              | Likely Cause                                      | Solution |
|--------------------------------------------|---------------------------------------------------|----------|
| `'M5Unified' does not name a type`        | Old M5Unified version or IDE cache issue         | Update M5Unified to latest, restart Arduino IDE completely |
| `class Sgp4 has no member named 'nextpass'` or ambiguous `init` / `findsat` | Wrong SGP4 library installed                     | Remove other Sgp4 libraries; install only **SparkFun SGP4 Arduino Library** |
| `no matching function for call to 'Sgp4::init'` | Using an incompatible SGP4 variant               | Use the SparkFun/Hopperpop version exactly |
| WiFiManager or LittleFS errors             | Missing ESP32 core components                    | Re-install M5Stack board package (it includes ESP32 core) |
| Upload fails / no port                     | Not in download mode or driver issue             | Long-press power button for download mode; install CP210x or CH340 driver if needed |

After successful upload, the device will boot and display the main tracking screen.

## ▶️ Running & First-Time Setup

1. After upload, the device boots into the **MAIN** tracking screen (defaults to ISS).
2. **Configure WiFi (required for first TLE download)**:
   - Tap **Setup** → **WiFi Configuration**
   - The device starts a captive portal (SSID: `M5PaperS3-Setup`)
   - Connect your phone/laptop to that SSID
   - Open a browser — it should redirect to the WiFiManager page
   - Select your home WiFi network and enter credentials
   - Save — the device restarts WiFi and returns to the main screen
3. **Set your location** (critical for accurate predictions):
   - Tap **Setup** → **Enter Maidenhead Grid** (recommended, supports 4/6 chars e.g. `FM18lw`) or **Enter Lat / Lon**
   - Use the on-screen keyboard and tap **Done**
   - Location is saved permanently in flash
4. **Select a satellite**:
   - Tap **Select Sat**
   - Browse pages (Prev/Next) and tap any satellite name
   - The device immediately begins tracking it (refreshes predictions)
5. **Refresh data**:
   - Tap **Refresh** anytime to force a TLE check + fresh predictions

Subsequent boots use the cached TLE file from LittleFS and only attempt an update if the data is older than ~24 hours (and WiFi is available). The device works completely offline after the first successful download.

## 🧠 How PaperSat Functions (Technical Deep Dive)

### High-Level Architecture
PaperSat is a classic embedded state-machine UI application with persistent storage, network I/O, and real-time orbital math running on a resource-constrained ESP32-S3 + e-ink display.

- **UI State Machine** (`enum Screen`): `MAIN`, `SAT_SELECT`, `SETUP_MENU`, `GRID_INPUT`, `LATLON_INPUT`, `TIME_INPUT`. All drawing and touch handling is dispatched from `handleTouch()` based on the current state and hardcoded button rectangles (optimized for 960×540 landscape after `setRotation(1)`).
- **Persistent Storage**:
  - `Preferences` ("sattracker" namespace): latitude, longitude, selected satellite name/NORAD, last successful TLE timestamp.
  - `LittleFS`: `/nasabare.txt` — the entire raw AMSAT TLE file for offline parsing.
- **Main Loop** (`loop()`): Calls `M5.update()`, `handleTouch()`, and (only on MAIN screen) checks a visibility-adaptive timer (15 s visible / 60 s hidden) to call `updateData()` + `drawMainScreen()`.

### 1. TLE Acquisition & Caching (`fetchTLE` + `parseTLEPayload`)
- On every `updateData()` (and thus periodically), checks:
  - Does `/nasabare.txt` exist in LittleFS?
  - Is `lastTLETime` (persisted) older than 24 h **and** NTP time is valid (`> 2021`) **and** WiFi is connected?
- If download needed: HTTP GET from `https://www.amsat.org/tle/current/nasabare.txt`, write raw payload to LittleFS, then parse.
- **Parser** scans for `1 ` lines, extracts satellite name (previous line) and 5-digit NORAD, stores up to 200 entries in `satList[]`. If the currently selected NORAD is found, its two TLE lines are copied into `currentTLE1`/`currentTLE2`.
- Smart time handling: if NTP hasn't synced yet (`now` is small), it still uses the freshly downloaded data but sets a RAM sentinel so it doesn't spam downloads in the same session. On next boot it will try again once time is valid.
- Result: status message + `lastTLEFetch` (millis-based age display) updated. Works offline forever after first cache.

### 2. Orbital Math & Pass Prediction (`predictPasses`)
- Uses the **Sgp4** library (SparkFun/Hopperpop port):
  - `sat.site(qth_lat, qth_lon, qth_alt)` — sets observer location
  - `sat.init(selectedName, currentTLE1, currentTLE2)` — loads the chosen satellite's elements
  - `sat.initpredpoint(now, 0.0)` — prepares the pass predictor
  - `sat.nextpass(&p, 40, false, 0.0)` — finds the next pass (library returns `passinfo` with `jdmax` = time of closest approach, `jdstop` = LOS, `maxelevation`)
- **Hybrid AOS calculation** (the clever part that makes eccentric orbits work reliably):
  - Library's `jdstart` (AOS) is sometimes unreliable.
  - Code therefore ignores it and manually searches **backward** from the peak (`jdmax`) in 30-second coarse steps, then 1-second fine steps, calling `sat.findsat(unixTime)` each time and checking `sat.satEl <= 0.0` to locate the precise AOS crossing.
  - LOS and max elevation from the library are trusted and used directly.
- Up to 8 future passes are stored in `passes[]` (AOS, LOS, maxEl). The UI only renders the next 3.
- `updateCurrentPosition()` simply does `sat.findsat(now)` to get live `satAz` / `satEl`.

### 3. Main Display & Polar Plot (`drawMainScreen`)
- **Polar geometry** (hardcoded center `cx=380, cy=280, r=190` on the rotated 960×540 canvas):
  - Elevation rings drawn as concentric circles (0° = outer horizon, inner = higher elevation, tiny center circle = near zenith).
  - 8 azimuth radials + text labels.
- **Satellite icon & arrow**:
  - If `sat.satEl > 0`: compute normalized elevation `eln = (90 - el) / 90`, then Cartesian `(x = cx + r*eln*sin(az), y = cy - r*eln*cos(az))`, draw filled square.
  - Direction arrow: `findsat(now + 45)` to get future position, draw a short line + arrowhead using `atan2`.
- **Pass trajectory overlay** (when satellite is below horizon):
  - Samples ~36 points across the *next* pass duration, calls `findsat` for each, and draws connected line segments (only while El > 0).
  - State is saved/restored so the live position isn't corrupted.
- **Pass list** (right side): rendered with `sprintf` for `HH:MM:SS → HH:MM:SS  XX.X°` (degree symbol via `drawDegreeSymbol` = small filled circle). A white `fillRect` clears only this region before redraw to reduce ghosting.
- Other elements: UTC time (via `gmtime`), battery %, status bar, three main buttons (Refresh / Select Sat / Setup).

### 4. Touch Input System
- `wasTouched(x,y,w,h)` helper checks the latest touch detail against axis-aligned rectangles.
- Each screen draws its own virtual keyboard (grid letters, numeric keypad, time entry with Del/Clr).
- `inputBuffer` accumulates characters; **Done** handler parses (e.g. `gridToLatLon`, `sscanf` for lat/lon/time), saves via `saveConfig()`, forces `updateData()`, and returns to MAIN.
- All coordinates are tuned for comfortable finger-sized targets on the e-ink display.

### 5. Time & Location Handling
- On boot: `configTime(0,0,"pool.ntp.org")` + `WiFi.begin()`.
- Manual time entry uses standard `mktime` + `settimeofday`.
- All orbital calculations are performed in Unix epoch UTC.
- Maidenhead decoder supports both 4-char and 6-char precision (adds 1/24° resolution for the last two letters).

### 6. E-Ink & Power Considerations
- Full `M5.Display.display()` only on screen changes or forced refresh.
- Dynamic areas use targeted `fillRect(TFT_WHITE, ...)` before redrawing text/lines.
- Polling interval automatically lengthens when the satellite is not visible, dramatically reducing power and e-ink wear.

This combination of library SGP4 power, custom AOS refinement, persistent local cache, and carefully optimized e-ink UI makes PaperSat a truly standalone field tool.

## 🔧 Troubleshooting

- **No WiFi / "No Wifi & No local TLE"**: Go to **Setup → WiFi Configuration** and run the captive portal. After saving credentials, tap **Refresh**.
- **Predictions seem wrong**: Double-check your Maidenhead grid or lat/lon (use 6-char grid for best accuracy). Tap **Refresh** after changing location.
- **Display looks stale / ghosting**: The code already uses partial clears; a full power-cycle or long-press of the hardware refresh button (if present) can help.
- **Time is wrong**: Use the **Set Time/Date** option in Setup, or ensure WiFi + NTP works.
- **Library / compile issues**: See the table in the Compilation section above. Always use the exact recommended library versions.

## 📄 License

This project is released under the **MIT License**.

## 🙏 Credits & Acknowledgments

- **M5Stack** — for the outstanding M5Paper S3 hardware and M5Unified library
- **Hopperpop / SparkFun** — SGP4 Arduino library (the foundation of all orbital math here)
- **tzapu** — WiFiManager
- **AMSAT** — for the convenient `nasabare.txt` TLE collection
- Dr. T.S. Kelso (CelesTrak) and the orbital mechanics community for algorithms and data standards
- The amateur satellite (AMSAT) and ham radio community for inspiration, testing, and feedback

---

**PaperSat** — Professional satellite tracking, right in the palm of your hand.

For questions, bug reports, or feature ideas, please open an issue on GitHub.

*Made with ❤️ for satellite enthusiasts everywhere.*
