#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Sgp4.h>
#include <time.h>

// ============== DEFAULTS (first boot) ==============
double qth_lat = 40.7128;
double qth_lon = -74.0060;
double qth_alt = 10.0;

String selectedName = "ISS (Zarya)";
String selectedNorad = "25544";

// Popular satellites (quick tap selection)
struct Satellite {
const char* name;
const char* norad;
};
Satellite satList[] = {
{"ISS (Zarya)", "25544"},
{"Tiangong CSS", "48274"},
{"Hubble Telescope", "20580"},
{"NOAA-20", "44432"},
{"Sentinel-1A", "39634"},
{"Landsat 8", "39084"}
};
const int satCount = sizeof(satList) / sizeof(satList[0]);

M5Unified M5;
Sgp4 sat;
Preferences prefs;

char currentTLE1[80], currentTLE2[80];

struct Pass {
time_t aos, tca, los;
double maxEl;
};
Pass passes[8];
int passCount = 0;

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 30000; // 30 seconds as requested

enum Screen { MAIN, SAT_SELECT };
Screen currentScreen = MAIN;

// Maidenhead grid → lat/lon converter (centers the square)
void gridToLatLon(const char* mgrid, double &lat, double &lon) {
String grid = String(mgrid);
if (grid.length() < 4) return;
grid.toUpperCase();

lon = (grid[0] - 'A') * 20.0 - 180.0;
lat = (grid[1] - 'A') * 10.0 - 90.0;

if (grid.length() >= 4) {
lon += (grid[2] - '0') * 2.0;
lat += (grid[3] - '0') * 1.0;
}
if (grid.length() >= 6) {
char sublon = tolower(grid[4]);
char sublat = tolower(grid[5]);
if (sublon >= 'a' && sublon <= 'x') lon += (sublon - 'a') * (2.0 / 24.0);
if (sublat >= 'a' && sublat <= 'x') lat += (sublat - 'a') * (1.0 / 24.0);
}
// Center of the square/subsquares
lon += 1.0;
lat += 0.5;
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

void openSetupPortal() {
WiFiManager wm;
wm.setConfigPortalTimeout(300);

WiFiManagerParameter param_grid("grid", "Maidenhead Grid (e.g. FN31pr)", "", 7);
WiFiManagerParameter param_lat("lat", "Latitude", String(qth_lat, 4).c_str(), 10);
WiFiManagerParameter param_lon("lon", "Longitude", String(qth_lon, 4).c_str(), 11);
WiFiManagerParameter param_norad("norad", "NORAD ID (any satellite)", selectedNorad.c_str(), 8);

wm.addParameter(&param_grid);
wm.addParameter(&param_lat);
wm.addParameter(&param_lon);
wm.addParameter(&param_norad);

M5.Display.clearDisplay();
M5.Display.setTextSize(2);
M5.Display.drawString("Setup Portal Open", 80, 100);
M5.Display.drawString("Connect phone to:", 80, 160);
M5.Display.drawString("M5PaperS3-Setup", 120, 200);
M5.Display.display();

if (wm.startConfigPortal("M5PaperS3-Setup")) {
String gridStr = param_grid.getValue();
if (gridStr.length() >= 4) {
gridToLatLon(gridStr.c_str(), qth_lat, qth_lon); // Maidenhead preferred
} else {
qth_lat = atof(param_lat.getValue());
qth_lon = atof(param_lon.getValue());
}
selectedNorad = param_norad.getValue();
if (selectedNorad.length() > 0) {
selectedName = "NORAD " + selectedNorad;
}
saveConfig();
}
WiFi.begin(); // reconnect to home WiFi
}

bool fetchTLE() {
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
return true;
}
}
http.end();
return false;
}

void predictPasses() {
passCount = 0;
time_t now = time(nullptr);
time_t future = now + 86400;
passinfo overpass;
while (now < future && passCount < 8) {
if (sat.predict(overpass, now, 5.0)) {
passes[passCount].aos = overpass.aos;
passes[passCount].tca = overpass.tca;
passes[passCount].los = overpass.los;
passes[passCount].maxEl = overpass.maxel;
passCount++;
now = overpass.los + 60;
} else {
now += 60;
}
}
}

void updateCurrentPosition() {
time_t now = time(nullptr);
sat.findsat(now);
}

void updateData() {
if (fetchTLE()) {
sat.init(currentTLE1, currentTLE2);
sat.setSite(qth_lat, qth_lon, qth_alt);
}
predictPasses();
updateCurrentPosition();
lastUpdate = millis();
}

void drawMainScreen() {
M5.Display.clearDisplay();
M5.Display.setTextColor(TFT_BLACK);
M5.Display.setTextSize(2);

M5.Display.drawString("Satellite Tracker", 20, 20);
M5.Display.drawString(selectedName.c_str(), 20, 55);

struct tm timeinfo;
getLocalTime(&timeinfo);
char timeStr[20];
sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
M5.Display.drawString(timeStr, 680, 20);

// Sky radar
int cx = 480, cy = 300, r = 210;
M5.Display.drawCircle(cx, cy, r, TFT_BLACK);
M5.Display.drawCircle(cx, cy, r / 2, TFT_BLACK);
M5.Display.drawString("N", cx - 10, cy - r - 25);
M5.Display.drawString("S", cx - 10, cy + r + 5);
M5.Display.drawString("E", cx + r + 10, cy - 10);
M5.Display.drawString("W", cx - r - 35, cy - 10);

double azRad = sat.satAz * PI / 180.0;
double elNorm = (90.0 - sat.satEl) / 90.0;
int px = cx + (int)(r * elNorm * sin(azRad));
int py = cy - (int)(r * elNorm * cos(azRad));
M5.Display.fillCircle(px, py, 12, TFT_RED);

char pos[80];
sprintf(pos, "Az: %.1f° El: %.1f°", sat.satAz, sat.satEl);
M5.Display.drawString(pos, 20, 520);

// Passes
M5.Display.drawString("Next Passes (UTC):", 20, 120);
for (int i = 0; i < passCount && i < 5; i++) {
struct tm* aosTm = gmtime(&passes[i].aos);
struct tm* losTm = gmtime(&passes[i].los);
char line[90];
sprintf(line, "%02d:%02d → %02d:%02d Max %.1f°",
aosTm->tm_hour, aosTm->tm_min,
losTm->tm_hour, losTm->tm_min, passes[i].maxEl);
M5.Display.drawString(line, 20, 160 + i * 38);
}

// Touch buttons
M5.Display.drawRect(680, 100, 240, 55, TFT_BLACK); M5.Display.drawString("Refresh", 710, 115);
M5.Display.drawRect(680, 180, 240, 55, TFT_BLACK); M5.Display.drawString("Select Sat", 695, 195);
M5.Display.drawRect(680, 260, 240, 55, TFT_BLACK); M5.Display.drawString("Setup", 740, 275);

M5.Display.display();
}

void drawSatSelectScreen() {
M5.Display.clearDisplay();
M5.Display.drawString("Select Satellite", 20, 20);

for (int i = 0; i < satCount; i++) {
int y = 80 + i * 50;
M5.Display.drawRect(20, y, 620, 45, TFT_BLACK);
M5.Display.drawString(satList[i].name, 40, y + 12);
}

int customY = 80 + satCount * 50;
M5.Display.drawRect(20, customY, 620, 45, TFT_BLACK);
M5.Display.drawString("Custom NORAD ID...", 40, customY + 12);

M5.Display.drawRect(680, 420, 240, 60, TFT_BLACK);
M5.Display.drawString("Back", 740, 440);

M5.Display.display();
}

bool wasTouched(int x, int y, int w, int h) {
if (!M5.Touch.getDetail().wasPressed()) return false;
auto t = M5.Touch.getDetail();
return (t.x >= x && t.x < x + w && t.y >= y && t.y < y + h);
}

void handleTouch() {
if (!M5.Touch.getDetail().wasPressed()) return;

if (currentScreen == MAIN) {
if (wasTouched(680, 100, 240, 55)) { // Refresh
updateData();
drawMainScreen();
} else if (wasTouched(680, 180, 240, 55)) { // Select Sat
currentScreen = SAT_SELECT;
drawSatSelectScreen();
} else if (wasTouched(680, 260, 240, 55)) { // Setup (location + custom NORAD)
openSetupPortal();
updateData();
drawMainScreen();
}
}
else if (currentScreen == SAT_SELECT) {
if (wasTouched(680, 420, 240, 60)) { // Back
currentScreen = MAIN;
drawMainScreen();
return;
}

// Popular satellites
for (int i = 0; i < satCount; i++) {
int y = 80 + i * 50;
if (wasTouched(20, y, 620, 45)) {
selectedName = satList[i].name;
selectedNorad = satList[i].norad;
saveConfig();
currentScreen = MAIN;
updateData();
drawMainScreen();
return;
}
}

// Custom NORAD
int customY = 80 + satCount * 50;
if (wasTouched(20, customY, 620, 45)) {
currentScreen = MAIN;
openSetupPortal();
updateData();
drawMainScreen();
return;
}
}
}

void setup() {
M5.begin();
M5.Display.setRotation(0);
M5.Display.setTextSize(2);
M5.Display.setTextColor(TFT_BLACK);

loadConfig();

WiFi.begin();
int attempts = 0;
while (WiFi.status() != WL_CONNECTED && attempts < 15) {
delay(500);
attempts++;
}

configTime(0, 0, "pool.ntp.org");
struct tm timeinfo;
while (!getLocalTime(&timeinfo)) delay(300);

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
