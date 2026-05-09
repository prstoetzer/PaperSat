#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Sgp4.h>
#include <time.h>

// ====================== CONFIG ======================
double qth_lat = 40.7128;
double qth_lon = -74.0060;
double qth_alt = 10.0;

String selectedName = "ISS (Zarya)";
String selectedNorad = "25544";

struct Satellite {
  const char* name;
  const char* norad;
};
Satellite satList[] = {
  {"ISS (Zarya)", "25544"},
  {"Tiangong CSS", "48274"},
  {"Hubble", "20580"},
  {"NOAA-20", "44432"},
  {"Sentinel-1A", "39634"},
  {"Landsat 8", "39084"}
};
const int satCount = sizeof(satList) / sizeof(satList[0]);

Sgp4 sat;
Preferences prefs;

char currentTLE1[80], currentTLE2[80];

struct Pass {
  time_t aos, los;
  double maxEl;
};
Pass passes[8];
int passCount = 0;

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 10000;  // 10 seconds

enum Screen { MAIN, SAT_SELECT, SETUP_MENU, GRID_INPUT, LATLON_INPUT, CUSTOM_NORAD };
Screen currentScreen = MAIN;

String inputBuffer = "";
String statusMsg = "Booting...";

// ====================== MAIDENHEAD ======================
void gridToLatLon(const char* mgrid, double &lat, double &lon) {
  String g = String(mgrid); g.toUpperCase();
  if (g.length() < 4) return;
  lon = (g[0]-'A')*20.0 - 180.0 + 1.0;
  lat = (g[1]-'A')*10.0 - 90.0 + 0.5;
  if (g.length() >= 4) {
    lon += (g[2]-'0')*2.0;
    lat += (g[3]-'0')*1.0;
  }
  if (g.length() >= 6) {
    lon += (tolower(g[4])-'a')*(2.0/24.0);
    lat += (tolower(g[5])-'a')*(1.0/24.0);
  }
}

void loadConfig() {
  prefs.begin("sattracker", true);
  qth_lat = prefs.getDouble("lat", 40.7128);
  qth_lon = prefs.getDouble("lon", -74.0060);
  selectedNorad = prefs.getString("norad", "25544");
  selectedName = prefs.getString("name", "ISS (Zarya)");
  prefs.end();
}

void saveConfig() {
  prefs.begin("sattracker", false);
  prefs.putDouble("lat", qth_lat);
  prefs.putDouble("lon", qth_lon);
  prefs.putString("norad", selectedNorad);
  prefs.putString("name", selectedName);
  prefs.end();
}

// ====================== TLE & ORBIT ======================
bool fetchTLE() {
  statusMsg = "Fetching TLE...";
  HTTPClient http;
  String url = "https://celestrak.org/NORAD/elements/gp.php?CATNR=" + selectedNorad + "&FORMAT=TLE";
  http.begin(url);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    int idx = payload.indexOf('\n');
    if (idx > 0) {
      payload.substring(0, idx).toCharArray(currentTLE1, 80);
      payload.substring(idx + 1).toCharArray(currentTLE2, 80);
      http.end();
      statusMsg = "TLE Loaded";
      return true;
    }
  }
  http.end();
  statusMsg = "TLE Failed";
  return false;
}

void predictPasses() {
  passCount = 0;
  passinfo p;
  sat.initpredpoint((unsigned long)time(nullptr), 0);
  while (passCount < 8) {
    if (sat.nextpass(&p, 20, false, 5.0)) {
      passes[passCount].aos = p.jdstart;
      passes[passCount].los = p.jdstop;
      passes[passCount].maxEl = p.maxelevation;
      passCount++;
    } else break;
  }
}

void updateCurrentPosition() {
  sat.findsat((unsigned long)time(nullptr));
}

void updateData() {
  if (WiFi.status() != WL_CONNECTED) {
    statusMsg = "WiFi disconnected";
    return;
  }
  if (fetchTLE()) {
    sat.init(selectedName.c_str(), currentTLE1, currentTLE2);
    sat.site(qth_lat, qth_lon, qth_alt);
    predictPasses();
    updateCurrentPosition();
    statusMsg = "Tracking " + selectedName;
  }
  lastUpdate = millis();
}

// ====================== BATTERY ======================
int getBatteryPercent() {
  float voltage = M5.Power.getBatteryVoltage() / 1000.0;
  int percent = (voltage - 3.4) / (4.2 - 3.4) * 100;
  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;
  return percent;
}

// ====================== TOUCH ======================
bool wasTouched(int x, int y, int w, int h) {
  if (!M5.Touch.getDetail().wasPressed()) return false;
  auto t = M5.Touch.getDetail();
  return (t.x >= x && t.x <= x + w && t.y >= y && t.y <= y + h);
}

// ====================== MAIN SCREEN ======================
void drawMainScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextSize(2);

  M5.Display.drawString("PaperSat", 20, 20);
  M5.Display.drawString(selectedName.c_str(), 20, 55);

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char ts[15];
  sprintf(ts, "%02d:%02d UTC", timeinfo.tm_hour, timeinfo.tm_min);
  M5.Display.drawString(ts, 620, 20);

  char bat[10];
  sprintf(bat, "%d%%", getBatteryPercent());
  M5.Display.drawString(bat, 760, 20);

  int cx = 380, cy = 280, r = 190;
  M5.Display.drawCircle(cx, cy, r, TFT_BLACK);
  M5.Display.drawCircle(cx, cy, r/2, TFT_BLACK);
  M5.Display.drawString("N", cx-10, cy-r-25);
  M5.Display.drawString("S", cx-10, cy+r+5);
  M5.Display.drawString("E", cx+r+10, cy-10);
  M5.Display.drawString("W", cx-r-35, cy-10);

  double az = sat.satAz * PI / 180.0;
  double eln = (90.0 - sat.satEl) / 90.0;
  int px = cx + (int)(r * eln * sin(az));
  int py = cy - (int)(r * eln * cos(az));
  M5.Display.fillCircle(px, py, 12, TFT_RED);

  char pos[60];
  sprintf(pos, "Az: %.1f°  El: %.1f°", sat.satAz, sat.satEl);
  M5.Display.drawString(pos, 20, 510);

  M5.Display.drawString("Next Passes (UTC):", 20, 120);
  for (int i = 0; i < passCount && i < 5; i++) {
    struct tm *aos = gmtime(&passes[i].aos);
    struct tm *los = gmtime(&passes[i].los);
    char line[80];
    sprintf(line, "%02d:%02d → %02d:%02d  %.1f°", 
            aos->tm_hour, aos->tm_min, los->tm_hour, los->tm_min, passes[i].maxEl);
    M5.Display.drawString(line, 20, 160 + i*38);
  }

  M5.Display.drawString(statusMsg.c_str(), 20, 480);

  M5.Display.drawRect(720, 100, 200, 55, TFT_BLACK); M5.Display.drawString("Refresh", 750, 115);
  M5.Display.drawRect(720, 180, 200, 55, TFT_BLACK); M5.Display.drawString("Select Sat", 735, 195);
  M5.Display.drawRect(720, 260, 200, 55, TFT_BLACK); M5.Display.drawString("Setup", 765, 275);

  M5.Display.display();
}

// ====================== SAT SELECT ======================
void drawSatSelectScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Select Satellite", 20, 20);

  for (int i = 0; i < satCount; i++) {
    int y = 80 + i * 55;
    M5.Display.drawRect(20, y, 620, 48, TFT_BLACK);
    M5.Display.drawString(satList[i].name, 40, y + 12);
  }

  int customY = 80 + satCount * 55;
  M5.Display.drawRect(20, customY, 620, 48, TFT_BLACK);
  M5.Display.drawString("Custom NORAD ID...", 40, customY + 12);

  M5.Display.drawRect(720, 420, 200, 60, TFT_BLACK);
  M5.Display.drawString("Back", 760, 440);
  M5.Display.display();
}

// ====================== SETUP MENU ======================
void drawSetupMenu() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Setup", 20, 20);

  M5.Display.drawRect(20, 80, 620, 55, TFT_BLACK); M5.Display.drawString("Enter Maidenhead Grid", 40, 92);
  M5.Display.drawRect(20, 160, 620, 55, TFT_BLACK); M5.Display.drawString("Enter Lat / Lon", 40, 172);
  M5.Display.drawRect(20, 240, 620, 55, TFT_BLACK); M5.Display.drawString("Custom NORAD ID", 40, 252);
  M5.Display.drawRect(20, 320, 620, 55, TFT_BLACK); M5.Display.drawString("WiFi Configuration", 40, 332);

  M5.Display.drawRect(720, 420, 200, 60, TFT_BLACK); M5.Display.drawString("Back", 760, 440);
  M5.Display.display();
}

// ====================== INPUT SCREENS ======================
void drawCustomNoradScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Enter NORAD ID", 20, 20);
  M5.Display.drawString(inputBuffer.c_str(), 40, 80);

  const char* numKeys[] = {"1","2","3","4","5","6","7","8","9","0"};
  for (int i = 0; i < 10; i++) {
    int x = 100 + (i % 5) * 80;
    int y = 160 + (i / 5) * 70;
    M5.Display.drawRect(x, y, 70, 60, TFT_BLACK);
    M5.Display.drawString(numKeys[i], x + 28, y + 18);
  }

  M5.Display.drawRect(720, 160, 200, 60, TFT_BLACK); M5.Display.drawString("Done", 760, 175);
  M5.Display.drawRect(720, 260, 200, 60, TFT_BLACK); M5.Display.drawString("Back", 760, 275);
  M5.Display.display();
}

void drawGridInputScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Enter Maidenhead Grid", 20, 20);
  M5.Display.drawString(inputBuffer.c_str(), 40, 80);

  const char* keys[] = {"A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","0","1","2","3","4","5","6","7","8","9"};
  for (int i = 0; i < 34; i++) {
    int x = 40 + (i % 10) * 65;
    int y = 160 + (i / 10) * 55;
    M5.Display.drawRect(x, y, 55, 45, TFT_BLACK);
    M5.Display.drawString(keys[i], x + 18, y + 12);
  }

  M5.Display.drawRect(720, 160, 200, 55, TFT_BLACK); M5.Display.drawString("Done", 760, 172);
  M5.Display.drawRect(720, 240, 200, 55, TFT_BLACK); M5.Display.drawString("Back", 760, 252);
  M5.Display.display();
}

void drawLatLonInputScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Enter Lat,Lon", 20, 20);
  M5.Display.drawString(inputBuffer.c_str(), 40, 80);

  const char* numKeys[] = {"7","8","9","4","5","6","1","2","3","0",".","-","+"};
  for (int i = 0; i < 13; i++) {
    int x = 40 + (i % 3) * 80;
    int y = 160 + (i / 3) * 55;
    M5.Display.drawRect(x, y, 70, 48, TFT_BLACK);
    M5.Display.drawString(numKeys[i], x + 25, y + 12);
  }

  M5.Display.drawRect(720, 160, 200, 55, TFT_BLACK); M5.Display.drawString("Done", 760, 172);
  M5.Display.drawRect(720, 240, 200, 55, TFT_BLACK); M5.Display.drawString("Back", 760, 252);
  M5.Display.display();
}

// ====================== TOUCH HANDLER ======================
void handleTouch() {
  if (!M5.Touch.getDetail().wasPressed()) return;
  auto t = M5.Touch.getDetail();

  if (currentScreen == MAIN) {
    if (wasTouched(720, 100, 200, 55)) { updateData(); drawMainScreen(); }
    else if (wasTouched(720, 180, 200, 55)) { currentScreen = SAT_SELECT; drawSatSelectScreen(); }
    else if (wasTouched(720, 260, 200, 55)) { currentScreen = SETUP_MENU; drawSetupMenu(); }
  }
  else if (currentScreen == SAT_SELECT) {
    if (wasTouched(720, 420, 200, 60)) { currentScreen = MAIN; drawMainScreen(); return; }

    for (int i = 0; i < satCount; i++) {
      int y = 80 + i * 55;
      if (wasTouched(20, y, 620, 48)) {
        selectedName = satList[i].name;
        selectedNorad = satList[i].norad;
        saveConfig();
        currentScreen = MAIN;
        updateData();
        drawMainScreen();
        return;
      }
    }
    int customY = 80 + satCount * 55;
    if (wasTouched(20, customY, 620, 48)) {
      inputBuffer = "";
      currentScreen = CUSTOM_NORAD;
      drawCustomNoradScreen();
      return;
    }
  }
  else if (currentScreen == SETUP_MENU) {
    if (wasTouched(720, 420, 200, 60)) { currentScreen = MAIN; drawMainScreen(); return; }

    if (wasTouched(20, 80, 620, 55)) { currentScreen = GRID_INPUT; inputBuffer = ""; drawGridInputScreen(); }
    else if (wasTouched(20, 160, 620, 55)) { currentScreen = LATLON_INPUT; inputBuffer = ""; drawLatLonInputScreen(); }
    else if (wasTouched(20, 240, 620, 55)) { currentScreen = CUSTOM_NORAD; inputBuffer = ""; drawCustomNoradScreen(); }
    else if (wasTouched(20, 320, 620, 55)) { openSetupPortal(); }
  }
  else if (currentScreen == CUSTOM_NORAD) {
    if (wasTouched(720, 260, 200, 60)) { currentScreen = SETUP_MENU; drawSetupMenu(); return; }
    if (wasTouched(720, 160, 200, 60)) {
      if (inputBuffer.length() > 0) {
        selectedNorad = inputBuffer;
        selectedName = "NORAD " + inputBuffer;
        saveConfig();
      }
      currentScreen = MAIN;
      updateData();
      drawMainScreen();
      return;
    }
    const char* numKeys[] = {"1","2","3","4","5","6","7","8","9","0"};
    for (int i = 0; i < 10; i++) {
      int x = 100 + (i % 5) * 80;
      int y = 160 + (i / 5) * 70;
      if (wasTouched(x, y, 70, 60)) {
        if (inputBuffer.length() < 8) inputBuffer += numKeys[i];
        drawCustomNoradScreen();
        return;
      }
    }
  }
  else if (currentScreen == GRID_INPUT) {
    if (wasTouched(720, 240, 200, 55)) { currentScreen = SETUP_MENU; drawSetupMenu(); return; }
    if (wasTouched(720, 160, 200, 55)) {
      if (inputBuffer.length() >= 4) gridToLatLon(inputBuffer.c_str(), qth_lat, qth_lon);
      saveConfig();
      currentScreen = MAIN;
      updateData();
      drawMainScreen();
      return;
    }
    const char* keys[] = {"A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","0","1","2","3","4","5","6","7","8","9"};
    for (int i = 0; i < 34; i++) {
      int x = 40 + (i % 10) * 65;
      int y = 160 + (i / 10) * 55;
      if (wasTouched(x, y, 55, 45)) {
        if (inputBuffer.length() < 6) inputBuffer += keys[i];
        drawGridInputScreen();
        return;
      }
    }
  }
  else if (currentScreen == LATLON_INPUT) {
    if (wasTouched(720, 240, 200, 55)) { currentScreen = SETUP_MENU; drawSetupMenu(); return; }
    if (wasTouched(720, 160, 200, 55)) {
      if (inputBuffer.length() > 0) {
        int comma = inputBuffer.indexOf(',');
        if (comma > 0) {
          qth_lat = inputBuffer.substring(0, comma).toDouble();
          qth_lon = inputBuffer.substring(comma + 1).toDouble();
          saveConfig();
        }
      }
      currentScreen = MAIN;
      updateData();
      drawMainScreen();
      return;
    }
    const char* numKeys[] = {"7","8","9","4","5","6","1","2","3","0",".","-","+"};
    for (int i = 0; i < 13; i++) {
      int x = 40 + (i % 3) * 80;
      int y = 160 + (i / 3) * 55;
      if (wasTouched(x, y, 70, 48)) {
        if (inputBuffer.length() < 20) inputBuffer += numKeys[i];
        drawLatLonInputScreen();
        return;
      }
    }
  }
}

void openSetupPortal() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  M5.Display.clearDisplay();
  M5.Display.drawString("WiFi Setup Portal", 80, 120);
  M5.Display.drawString("Connect to M5PaperS3-Setup", 40, 200);
  M5.Display.display();
  wm.startConfigPortal("M5PaperS3-Setup");
  WiFi.begin();
  statusMsg = "WiFi Saved";
}

void setup() {
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_BLACK);

  loadConfig();
  WiFi.begin();
  configTime(0, 0, "pool.ntp.org");

  updateData();
  drawMainScreen();
}

void loop() {
  M5.update();
  handleTouch();

  if (currentScreen == MAIN && millis() - lastUpdate > UPDATE_INTERVAL) {
    updateData();
    drawMainScreen();
  }
  delay(50);
}
