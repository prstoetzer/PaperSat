#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Sgp4.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <LittleFS.h>
#include <ArduinoJson.h>  // Install via Library Manager: "ArduinoJson" by Benoit Blanchon

// ====================== CONFIG ======================
double qth_lat = 38.8626;
double qth_lon = -77.0562;
double qth_alt = 10.0;

String selectedName = "ISS";
String selectedNorad = "25544";

struct Satellite {
  char name[25];
  char norad[10];
};
Satellite satList[200];
int satCount = 0;
int currentSatPage = 0;
const int satsPerPage = 10;

Sgp4 sat;
Preferences prefs;

char currentTLE1[80], currentTLE2[80];
time_t lastTLETime = 0;
bool forceTLEUpdate = false;

struct Pass {
  time_t aos, los;
  double maxEl;
};
Pass passes[8];
int passCount = 0;

unsigned long lastUpdate = 0;

// Threshold for considering time(nullptr) valid (post 2021 to detect unset NTP time)
const time_t TLE_TIME_VALID_THRESHOLD = 1609459200LL; // 2021-01-01 UTC

enum Screen { MAIN, SAT_SELECT, SETUP_MENU, GRID_INPUT, LATLON_INPUT, TIME_INPUT };
Screen currentScreen = MAIN;

String inputBuffer = "";
String statusMsg = "Booting...";

// ====================== FORWARD DECLARATIONS ======================
void drawMainScreen();
void drawDegreeSymbol(int16_t x, int16_t y);

// ====================== HELPER ======================
void drawDegreeSymbol(int16_t x, int16_t y) {
  M5.Display.fillCircle(x + 3, y + 4, 3, TFT_BLACK);
}

time_t jdToUnix(double jd) {
  return (jd - 2440587.5) * 86400.0;
}

void setSystemTime(int year, int mon, int day, int hour, int min, int sec) {
  struct tm t;
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;
  t.tm_isdst = 0;
  time_t epoch = mktime(&t);
  if (epoch != (time_t)-1) {
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
}

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

void latLonToGrid(double lat, double lon, char* gridOut) {
  // Normalize longitude to -180..180
  lon = fmod(lon + 180.0, 360.0);
  if (lon < 0) lon += 360.0;
  lon -= 180.0;

  lat = fmax(-90.0, fmin(90.0, lat));

  int fieldLon = (int)((lon + 180.0) / 20.0);
  int fieldLat = (int)((lat + 90.0) / 10.0);

  double squareLon = fmod((lon + 180.0), 20.0) / 2.0;
  double squareLat = fmod((lat + 90.0), 10.0);

  int subsquareLon = (int)(squareLon * 12.0);
  int subsquareLat = (int)(squareLat * 24.0);

  gridOut[0] = 'A' + fieldLon;
  gridOut[1] = 'A' + fieldLat;
  gridOut[2] = '0' + (int)squareLon;
  gridOut[3] = '0' + (int)squareLat;
  gridOut[4] = 'a' + subsquareLon;
  gridOut[5] = 'a' + subsquareLat;
  gridOut[6] = '\0';
}

// ====================== WIFI GEOLOCATION ======================
bool autoLocateViaWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    statusMsg = "WiFi not connected for location";
    drawMainScreen();
    return false;
  }
  HTTPClient http;
  // ip-api.com is reliable, no key needed for moderate use, returns clean JSON
  http.begin("http://ip-api.com/json/?fields=status,lat,lon,city,country");
  http.setTimeout(12000);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    http.end();
    DynamicJsonDocument doc(1536);
    DeserializationError err = deserializeJson(doc, payload);
    if (!err && doc["status"] == "success" && doc.containsKey("lat") && doc.containsKey("lon")) {
      qth_lat = doc["lat"].as<double>();
      qth_lon = doc["lon"].as<double>();
      saveConfig();

      char grid[7];
      latLonToGrid(qth_lat, qth_lon, grid);
      statusMsg = "WiFi Loc: " + String(grid);
      drawMainScreen();
      return true;
    }
  } else {
    http.end();
  }
  statusMsg = "WiFi geolocation failed";
  drawMainScreen();
  return false;
}

void loadConfig() {
  prefs.begin("sattracker", true);
  qth_lat = prefs.getDouble("lat", 40.7128);
  qth_lon = prefs.getDouble("lon", -74.0060);
  selectedNorad = prefs.getString("norad", "25544");
  selectedName = prefs.getString("name", "ISS");
  lastTLETime = prefs.getULong("lastTLE", 0);
  prefs.getString("tle1", currentTLE1, sizeof(currentTLE1));
  prefs.getString("tle2", currentTLE2, sizeof(currentTLE2));
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

// ====================== GP ORBITAL ELEMENTS -> TLE ======================
// AMSAT's daily-bulletin.json "tle" text field is being deprecated (the 5-digit
// NORAD catalog runs out ~2026, after which new objects get 6-digit catalog
// numbers that don't fit the TLE text format). The JSON still carries every
// discrete GP/OMM orbital element, so we rebuild the two standard SGP4 TLE lines
// from those fields. The reconstructed lines are byte-format-identical to the
// classic AMSAT TLE the SGP4 library already accepts.

// Standard TLE checksum over the first 68 columns: digits add their value,
// a '-' adds 1, everything else adds 0; result mod 10.
static char tleChecksum(const char* line) {
  int sum = 0;
  for (int i = 0; i < 68 && line[i]; i++) {
    char c = line[i];
    if (c >= '0' && c <= '9') sum += c - '0';
    else if (c == '-') sum += 1;
  }
  return '0' + (sum % 10);
}

// Format a value into the 8-char TLE assumed-decimal exponential field used by
// BSTAR and the 2nd derivative, e.g. 0.0001293 -> " 12930-3" (= .12930e-3).
static void tleExpField(double value, char* out) {
  if (value == 0.0 || !isfinite(value)) { strcpy(out, " 00000-0"); return; }
  char sign = (value < 0) ? '-' : ' ';
  double a = fabs(value);
  int exp = 0;
  while (a >= 1.0) { a /= 10.0; exp++; }
  while (a < 0.1)  { a *= 10.0; exp--; }
  long mant = lround(a * 100000.0);
  if (mant >= 100000) { mant /= 10; exp++; }   // guard rounding overflow
  sprintf(out, "%c%05ld%c%d", sign, mant, (exp < 0) ? '-' : '+', abs(exp));
}

// Convert "YYYY-MM-DD HH:MM:SS.ssssss" (also accepts the 'T' separator) into a
// 2-digit epoch year and fractional day-of-year, as needed for TLE columns 19-32.
static bool parseGPEpoch(const char* epoch, int &yy, double &doy) {
  int Y, Mo, D, h, m; double s = 0.0;
  int n = sscanf(epoch, "%d-%d-%d %d:%d:%lf", &Y, &Mo, &D, &h, &m, &s);
  if (n < 6) n = sscanf(epoch, "%d-%d-%dT%d:%d:%lf", &Y, &Mo, &D, &h, &m, &s);
  if (n < 5) return false;
  static const int cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
  bool leap = (Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0));
  int day = cum[Mo - 1] + D + ((leap && Mo > 2) ? 1 : 0);
  doy = (double)day + (h * 3600.0 + m * 60.0 + s) / 86400.0;
  yy = Y % 100;
  return true;
}

// Build standard TLE line1/line2 (NUL-terminated, 69 cols) from a GP JSON object.
// Returns false if the mandatory elements are missing/invalid.
static bool buildTLEFromGP(JsonObject s, char* tle1, char* tle2) {
  const char* epochC = s["EPOCH"] | "";
  if (strlen(epochC) < 10) return false;

  int yy; double doy;
  if (!parseGPEpoch(epochC, yy, doy)) return false;

  double mm = s["MEAN_MOTION"].as<double>();
  if (mm <= 0.0) return false;  // no usable elements

  long catnum = s["NORAD_CAT_ID"].as<long>();
  // SGP4 math never uses the catalog number; clamp to 5 cols so the fixed-width
  // TLE layout stays valid even once 6-digit (100000+) catalog numbers appear.
  long catCol = (catnum > 99999) ? (catnum % 100000) : catnum;

  char cls = ((const char*)(s["CLASSIFICATION_TYPE"] | "U"))[0];
  if (cls == 0) cls = 'U';
  char eph = ((const char*)(s["EPHEMERIS_TYPE"] | "0"))[0];
  if (eph == 0) eph = '0';

  // International designator from OBJECT_ID "YYYY-NNNAAA" -> "YYNNNAAA" (8 cols).
  char intl[9] = "        ";
  const char* oid = s["OBJECT_ID"] | "";
  const char* dash = strchr(oid, '-');
  if (dash && (dash - oid) >= 4) {
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%c%c%s", oid[2], oid[3], dash + 1);
    snprintf(intl, sizeof(intl), "%-8.8s", tmp);
  }

  double ndot    = s["MEAN_MOTION_DOT"].as<double>();
  double ndotdot = s["MEAN_MOTION_DDOT"].as<double>();
  double bstar   = s["BSTAR"].as<double>();
  double incl    = s["INCLINATION"].as<double>();
  double raan    = s["RA_OF_ASC_NODE"].as<double>();
  double ecc     = s["ECCENTRICITY"].as<double>();
  double argp    = s["ARG_OF_PERICENTER"].as<double>();
  double ma      = s["MEAN_ANOMALY"].as<double>();
  long   elset   = (long)(s["ELEMENT_SET_NO"] | 0.0);
  long   revnum  = s["REV_AT_EPOCH"].as<long>() % 100000;

  char ndotStr[16];
  sprintf(ndotStr, "%c.%08ld", (ndot < 0) ? '-' : ' ', lround(fabs(ndot) * 1e8));
  char ddotStr[10], bstarStr[10];
  tleExpField(ndotdot, ddotStr);
  tleExpField(bstar, bstarStr);

  // ---- Line 1 (68-char body + checksum) ----
  sprintf(tle1, "1 %5ld%c %-8s %02d%012.8f %s %s %s %c %4ld",
          catCol, cls, intl, yy, doy, ndotStr, ddotStr, bstarStr, eph, elset);
  tle1[68] = tleChecksum(tle1);
  tle1[69] = '\0';

  // ---- Line 2 (eccentricity is 7 digits with an assumed leading decimal) ----
  long eccCol = lround(ecc * 1e7);
  sprintf(tle2, "2 %5ld %8.4f %8.4f %07ld %8.4f %8.4f %11.8f%5ld",
          catCol, incl, raan, eccCol, argp, ma, mm, revnum);
  tle2[68] = tleChecksum(tle2);
  tle2[69] = '\0';

  return true;
}

// ====================== GP Data from AMSAT daily-bulletin.json (orbital elements -> TLE for SGP4) ======================
bool parseGPJson(const String& payload) {
  satCount = 0;
  DynamicJsonDocument doc(49152);  // Sufficient for ~89K daily-bulletin.json; uses PSRAM on M5Paper S3 if available
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    // JSON parse failed; could log but no Serial in normal run
    return false;
  }

  JsonArray sats;
  if (doc.is<JsonArray>()) {
    sats = doc.as<JsonArray>();
  } else if (doc["satellites"].is<JsonArray>()) {
    sats = doc["satellites"].as<JsonArray>();
  } else if (doc["data"].is<JsonArray>()) {
    sats = doc["data"].as<JsonArray>();
  } else if (doc["GP"].is<JsonArray>()) {
    sats = doc["GP"].as<JsonArray>();
  } else if (doc["elements"].is<JsonArray>()) {
    sats = doc["elements"].as<JsonArray>();
  } else {
    return false; // unknown structure
  }

  for (JsonObject s : sats) {
    if (satCount >= 200) break;

    // Look up the first non-empty value from a list of JSON keys, converting
    // numeric values to their string form. AMSAT now serializes NORAD_CAT_ID as
    // an integer rather than a quoted string, so reading it as const char* alone
    // would silently yield "" and reject every satellite.
    String nameStr, noradStr;
    const char* nameKeys[]  = {"AMSAT_NAME", "OBJECT_NAME", "name", "SATNAME", "title"};
    const char* noradKeys[] = {"NORAD_CAT_ID", "norad", "CATNR", "NORAD", "id"};
    for (auto k : nameKeys) {
      JsonVariant v = s[k];
      if (v.isNull()) continue;
      String out = v.as<String>();
      out.trim();
      if (out.length() > 0 && out != "null") { nameStr = out; break; }
    }
    for (auto k : noradKeys) {
      JsonVariant v = s[k];
      if (v.isNull()) continue;
      String out = v.as<String>();
      out.trim();
      if (out.length() > 0 && out != "null") { noradStr = out; break; }
    }
    if (nameStr.length() > 0 && noradStr.length() > 0) {
      nameStr.toCharArray(satList[satCount].name, sizeof(satList[satCount].name));
      noradStr.toCharArray(satList[satCount].norad, sizeof(satList[satCount].norad));

      // If this is the currently selected satellite, build its TLE from the
      // discrete GP orbital-element fields (MEAN_MOTION, ECCENTRICITY, EPOCH,
      // INCLINATION, etc.) instead of the deprecated "tle" text field.
      if (noradStr == selectedNorad) {
        char t1[80], t2[80];
        bool built = buildTLEFromGP(s, t1, t2);

        // Transitional fallback: if the GP element fields are absent but the
        // legacy "tle" text field is still present, parse that instead.
        if (!built) {
          String tleStr = s["tle"] | "";
          if (tleStr.length() > 20) {
            int firstNewline = tleStr.indexOf('\n');
            if (firstNewline > 0) {
              int secondNewline = tleStr.indexOf('\n', firstNewline + 1);
              if (secondNewline > firstNewline) {
                String line1 = tleStr.substring(firstNewline + 1, secondNewline);
                String line2 = tleStr.substring(secondNewline + 1);
                line1.trim();
                line2.trim();
                line1.toCharArray(t1, sizeof(t1));
                line2.toCharArray(t2, sizeof(t2));
                built = (strlen(t1) > 60 && strlen(t2) > 60);
              }
            }
          }
        }

        if (built) {
          strncpy(currentTLE1, t1, sizeof(currentTLE1));
          strncpy(currentTLE2, t2, sizeof(currentTLE2));
          currentTLE1[sizeof(currentTLE1) - 1] = '\0';
          currentTLE2[sizeof(currentTLE2) - 1] = '\0';

          // Also save to Preferences for offline use
          prefs.begin("sattracker", false);
          prefs.putString("tle1", currentTLE1);
          prefs.putString("tle2", currentTLE2);
          prefs.end();
        }
      }

      satCount++;
    }
  }
  return satCount > 0;
}

bool fetchTLE() {
  bool haveLocal = LittleFS.exists("/daily-bulletin.json");
  time_t now = time(nullptr);
  bool timeValid = (now > TLE_TIME_VALID_THRESHOLD);

  bool needDownload = false;
  bool forceThisTime = forceTLEUpdate;
  forceTLEUpdate = false;  // reset after this call
  if (WiFi.status() == WL_CONNECTED) {
    if (forceThisTime || lastTLETime == 0 || (timeValid && (now - lastTLETime > 86400))) {
      needDownload = true;
    } else if (!haveLocal) {
      // No local cache file persisted (e.g. LittleFS write issue); retry periodically (every ~1h)
      // to acquire GP data without hammering AMSAT server every 60s on repeated refresh
      if (lastTLETime == 0 || (timeValid && (now - lastTLETime > 3600))) {
        needDownload = true;
      }
    }
  }

  if (needDownload && WiFi.status() == WL_CONNECTED) {
    statusMsg = "Downloading AMSAT GP data...";
    drawMainScreen();

    HTTPClient http;
    http.begin("https://newark192.amsat.org/gpdata/current/daily-bulletin.json");
    http.setTimeout(25000);
    int code = http.GET();

    if (code == HTTP_CODE_OK) {
      String payload = http.getString();
      http.end();

      // Save to local LittleFS for offline use (the curated amateur satellite GP bulletin)
      File f = LittleFS.open("/daily-bulletin.json", "w");
      if (f) {
        f.print(payload);
        f.close();
      }

      if (now > TLE_TIME_VALID_THRESHOLD) {
        lastTLETime = now;
        prefs.begin("sattracker", false);
        prefs.putULong("lastTLE", (unsigned long)lastTLETime);
        prefs.end();
      } else {
        lastTLETime = 1;  // non-zero sentinel (RAM only)
      }

      if (parseGPJson(payload)) {
        // Successfully parsed satellite list + TLE directly from AMSAT GP bulletin.
        // No Celestrak call is made (purely local parsing).
        if (strlen(currentTLE1) > 60 && strlen(currentTLE2) > 60) {
          struct tm t = *gmtime(&now);
          char msg[50];
          sprintf(msg, "GP Data %02d/%02d/%04d %02d:%02d UTC", 
                  t.tm_mon + 1, t.tm_mday, t.tm_year + 1900,
                  t.tm_hour, t.tm_min);
          statusMsg = msg;
          return true;
        } else {
          statusMsg = "GP list loaded but no TLE for selected satellite";
          return (strlen(currentTLE1) > 10); // use cached TLE if available
        }
      } else {
        statusMsg = "GP JSON parse failed after download";
        return false;
      }
    } else {
      http.end();
      statusMsg = "Download failed (code " + String(code) + "), using local...";
      if (now > TLE_TIME_VALID_THRESHOLD) {
        lastTLETime = now;
        prefs.begin("sattracker", false);
        prefs.putULong("lastTLE", (unsigned long)lastTLETime);
        prefs.end();
      } else {
        lastTLETime = 1;
      }
      // fall through to local load
    }
  }

  // Fallback / normal path: load from local JSON file (offline operation with last known GP list + cached TLE lines)
  if (haveLocal) {
    File f = LittleFS.open("/daily-bulletin.json", "r");
    if (f) {
      String payload = f.readString();
      f.close();
      if (parseGPJson(payload)) {
        if (needDownload) {
          statusMsg = "Using local GP data (update failed)";
        } else {
          statusMsg = "Using cached local GP data";
        }
        return (strlen(currentTLE1) > 10);
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED && !haveLocal) {
    statusMsg = "No Wifi & No local GP data";
  } else {
    statusMsg = "No GP data available";
  }
  return false;
}

void predictPasses() {
  passCount = 0;
  sat.site(qth_lat, qth_lon, qth_alt);

  passinfo p;
  // Use library's pass finder (reliable for detecting passes, maxEl, and LOS/jdstop)
  // but compute AOS manually from the peak because library jdstart is buggy for some sats like RS-44
  sat.initpredpoint((unsigned long)time(nullptr), 0.0);

  while (passCount < 8) {
    if (!sat.nextpass(&p, 40, false, 0.0)) {
      break; // no more passes found
    }
    // Only accept passes with reasonable max elevation and valid stop > max time
    if (p.maxelevation > 0.5 && p.jdstop > p.jdmax) {
      // Library provides good p.jdmax, p.jdstop (LOS), p.maxelevation
      // Ignore p.jdstart (often wrongly equals LOS or peak time)
      time_t peakTime = jdToUnix(p.jdmax);
      time_t losTimeLib = jdToUnix(p.jdstop);

      // Manually find AOS by searching backward from peak until elevation drops to <=0
      time_t aosTime = peakTime;
      const long COARSE_BACK_SEC = 30;
      bool crossedBelow = false;
      for (long back = 0; back < 2 * 3600; back += COARSE_BACK_SEC) { // search up to 2 hours back
        time_t tt = peakTime - back;
        if (tt < time(nullptr) - 3600) break;
        sat.findsat((unsigned long)tt);
        if (sat.satEl <= 0.0) {
          aosTime = tt;
          crossedBelow = true;
          break;
        }
      }
      if (!crossedBelow) {
        continue; // couldn't find AOS, skip this pass
      }

      // Refine AOS: step forward from the rough below point to find first time El > 0
      time_t refinedAOS = aosTime;
      for (int d = 0; d < 120; ++d) { // up to 2 minutes refine window
        time_t tt = aosTime + d;
        sat.findsat((unsigned long)tt);
        if (sat.satEl > 0.0) {
          refinedAOS = tt;
          break;
        }
      }

      // Use library's LOS (confirmed correct by user) and maxEl
      time_t refinedLOS = losTimeLib;

      if (refinedLOS > refinedAOS + 30) {
        passes[passCount].aos = refinedAOS;
        passes[passCount].los = refinedLOS;
        passes[passCount].maxEl = p.maxelevation;
        passCount++;
      }
    }
  }
}

void updateCurrentPosition() {
  sat.findsat((unsigned long)time(nullptr));
}

void updateData() {
  if (WiFi.status() != WL_CONNECTED) {
    statusMsg = "WiFi disconnected - retrying...";
    WiFi.begin();
    delay(1500);
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

// ====================== SATELLITE ICON ======================
void drawSatelliteIcon(int x, int y, int size) {
  M5.Display.fillRect(x - size/2, y - size/2, size, size, TFT_BLACK);
}

// ====================== MAIN SCREEN ======================
void drawMainScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextColor(TFT_BLACK);

  M5.Display.setTextSize(3);
  M5.Display.drawString("PaperSat", 20, 15);
  M5.Display.setTextSize(2);
  M5.Display.drawString(selectedName.c_str(), 20, 55);

  int cx = 380, cy = 280, r = 190;
  M5.Display.drawCircle(cx, cy, r, TFT_BLACK);
  M5.Display.drawCircle(cx, cy, r/2, TFT_BLACK);
  M5.Display.drawCircle(cx, cy, 18, TFT_BLACK);  // small center circle for zenith / refined appearance

  for (int i = 0; i < 8; i++) {
    float angle = i * 45.0 * PI / 180.0;
    int x1 = cx + (int)(r * 0.2 * sin(angle));
    int y1 = cy - (int)(r * 0.2 * cos(angle));
    int x2 = cx + (int)(r * sin(angle));
    int y2 = cy - (int)(r * cos(angle));
    M5.Display.drawLine(x1, y1, x2, y2, TFT_BLACK);
  }

  M5.Display.setTextSize(2);
  M5.Display.drawString("N", cx-8, cy-r-28);
  M5.Display.drawString("S", cx-8, cy+r+8);
  M5.Display.drawString("E", cx+r+12, cy-8);
  M5.Display.drawString("W", cx-r-38, cy-8);

  // Determine which pass to plot on the polar map
  int passToPlot = -1;
  if (passCount > 0) {
    time_t now = time(nullptr);
    if (sat.satEl > 0) {
      // Find the current ongoing pass (may have AOS in past)
      for (int i = 0; i < passCount; i++) {
        if (passes[i].aos <= now && passes[i].los >= now) {
          passToPlot = i;
          break;
        }
      }
    } else {
      // Plot the next upcoming pass
      passToPlot = 0;
    }
  }

  // Draw path for current pass (if visible) or next pass (if not visible)
  if (passToPlot >= 0) {
    time_t startT = passes[passToPlot].aos;
    time_t endT = passes[passToPlot].los;
    long duration = endT - startT;
    if (duration > 0) {
      const int numPoints = 36;
      long step = duration / (numPoints - 1);
      if (step < 15) step = 15;
      int prevX = -1, prevY = -1;
      double savedAz = sat.satAz;
      double savedEl = sat.satEl;
      for (int i = 0; i < numPoints; i++) {
        time_t t = startT + (long)i * step;
        if (t > endT) t = endT;
        sat.findsat((unsigned long)t);
        if (sat.satEl > 0.0) {
          double az = sat.satAz * PI / 180.0;
          double eln = (90.0 - sat.satEl) / 90.0;
          int px = cx + (int)(r * eln * sin(az));
          int py = cy - (int)(r * eln * cos(az));
          if (prevX >= 0) {
            M5.Display.drawLine(prevX, prevY, px, py, TFT_BLACK);
          }
          prevX = px;
          prevY = py;
        } else {
          prevX = -1;
        }
      }
      // Restore current satellite position state
      sat.satAz = savedAz;
      sat.satEl = savedEl;
    }
  }

  // Plot current satellite position (only if above horizon) with direction arrow
  if (sat.satEl > 0) {
    double az = sat.satAz * PI / 180.0;
    double eln = (90.0 - sat.satEl) / 90.0;
    int px = cx + (int)(r * eln * sin(az));
    int py = cy - (int)(r * eln * cos(az));
    drawSatelliteIcon(px, py, 18);

    // Direction arrow: compute position ~45s ahead and draw arrow indicating travel direction
    time_t nowT = time(nullptr);
    time_t futureT = nowT + 45;
    if (passToPlot >= 0 && passes[passToPlot].los < futureT + 10) {
      futureT = passes[passToPlot].los - 5;
    }
    if (futureT > nowT + 5) {
      double savedAz2 = sat.satAz;
      double savedEl2 = sat.satEl;
      sat.findsat((unsigned long)futureT);
      if (sat.satEl > 0.0) {
        double azf = sat.satAz * PI / 180.0;
        double elnf = (90.0 - sat.satEl) / 90.0;
        int pxf = cx + (int)(r * elnf * sin(azf));
        int pyf = cy - (int)(r * elnf * cos(azf));
        int dx = pxf - px;
        int dy = pyf - py;
        double len = sqrt(dx * dx + dy * dy);
        if (len > 3.0) {
          double scale = 25.0 / len;
          int ax = px + (int)(dx * scale);
          int ay = py + (int)(dy * scale);
          M5.Display.drawLine(px, py, ax, ay, TFT_BLACK);
          // Simple arrow head
          double angle = atan2(dy, dx);
          double asz = 11.0;  // slightly larger arrowhead for visibility
          int hx1 = ax - (int)(asz * cos(angle - 0.4));
          int hy1 = ay - (int)(asz * sin(angle - 0.4));
          int hx2 = ax - (int)(asz * cos(angle + 0.4));
          int hy2 = ay - (int)(asz * sin(angle + 0.4));
          M5.Display.drawLine(ax, ay, hx1, hy1, TFT_BLACK);
          M5.Display.drawLine(ax, ay, hx2, hy2, TFT_BLACK);
        }
      }
      sat.satAz = savedAz2;
      sat.satEl = savedEl2;
    }
  }

  // Azimuth + Elevation with drawn degree symbol
  char buf[32];
  sprintf(buf, "Az: %.1f", sat.satAz);
  M5.Display.drawString(buf, 20, 510);
  int x = 20 + M5.Display.textWidth(buf);
  drawDegreeSymbol(x, 510);

  sprintf(buf, "   El: %.1f", sat.satEl);
  x = x + 14;
  M5.Display.drawString(buf, x, 510);
  x = x + M5.Display.textWidth(buf);
  drawDegreeSymbol(x, 510);

  // Next 3 Passes - Lower Right
  M5.Display.setTextSize(2);
  // Clear the passes list area (including TLE msg below) to ensure clean e-ink update (prevents ghosting of old text)
  M5.Display.fillRect(620, 340, 340, 160, TFT_WHITE);
  M5.Display.drawString("Next Passes (UTC):", 620, 340);

  for (int i = 0; i < passCount && i < 3; i++) {
    struct tm aos_tm = *gmtime(&passes[i].aos);
    struct tm los_tm = *gmtime(&passes[i].los);
    char line[80];
    sprintf(line, "%02d:%02d:%02d -> %02d:%02d:%02d  %.1f", 
            aos_tm.tm_hour, aos_tm.tm_min, aos_tm.tm_sec,
            los_tm.tm_hour, los_tm.tm_min, los_tm.tm_sec, passes[i].maxEl);
    M5.Display.drawString(line, 620, 380 + i*38);
    int px = 620 + M5.Display.textWidth(line);
    drawDegreeSymbol(px, 380 + i*38);
  }

  // GP data timestamp
  if (lastTLETime > TLE_TIME_VALID_THRESHOLD) {
    struct tm t = *gmtime(&lastTLETime);
    char tleStr[50];
    sprintf(tleStr, "GP Data %02d/%02d/%04d %02d:%02d UTC", 
            t.tm_mon + 1, t.tm_mday, t.tm_year + 1900,
            t.tm_hour, t.tm_min);
    M5.Display.drawString(tleStr, 620, 510);
  }
  M5.Display.drawString(statusMsg.c_str(), 20, 480);

  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char ts[15];
  sprintf(ts, "%02d:%02d UTC", timeinfo.tm_hour, timeinfo.tm_min);
  M5.Display.drawString(ts, 620, 20);

  char bat[10];
  sprintf(bat, "%d%%", getBatteryPercent());
  M5.Display.drawString(bat, 760, 20);

  M5.Display.drawRoundRect(720, 100, 200, 55, 8, TFT_BLACK);
  M5.Display.drawString("Refresh", 755, 115);
  M5.Display.drawRoundRect(720, 180, 200, 55, 8, TFT_BLACK);
  M5.Display.drawString("Select Sat", 740, 195);
  M5.Display.drawRoundRect(720, 260, 200, 55, 8, TFT_BLACK);
  M5.Display.drawString("Setup", 770, 275);

  M5.Display.display();
}

// ====================== OTHER SCREENS ======================
void drawSatSelectScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Select Satellite", 20, 20);

  int startIdx = currentSatPage * satsPerPage;
  int numOnPage = (satsPerPage < (satCount - startIdx) ? satsPerPage : (satCount - startIdx));
  for (int j = 0; j < numOnPage; j++) {
    int i = startIdx + j;
    int y = 60 + j * 40;
    M5.Display.drawRect(20, y, 620, 36, TFT_BLACK);
    M5.Display.drawString(satList[i].name, 40, y + 10);
  }

  // Page navigation on right side (if multiple pages)
  int totalPages = (satCount + satsPerPage - 1) / satsPerPage;
  if (totalPages > 1) {
    char pageStr[20];
    sprintf(pageStr, "Page %d/%d", currentSatPage + 1, totalPages);
    M5.Display.drawString(pageStr, 720, 80);
    if (currentSatPage > 0) {
      M5.Display.drawRoundRect(720, 130, 200, 55, 8, TFT_BLACK);
      M5.Display.drawString("Prev", 780, 145);
    }
    if (currentSatPage < totalPages - 1) {
      M5.Display.drawRoundRect(720, 200, 200, 55, 8, TFT_BLACK);
      M5.Display.drawString("Next", 780, 215);
    }
  }

  M5.Display.drawRoundRect(720, 280, 200, 55, 8, TFT_BLACK);
  M5.Display.drawString("Update GP", 745, 295);

  M5.Display.drawRect(720, 470, 200, 55, TFT_BLACK);
  M5.Display.drawString("Back", 760, 488);
  M5.Display.display();
}

void drawSetupMenu() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Setup", 20, 20);

  M5.Display.drawRect(20, 80, 620, 55, TFT_BLACK); M5.Display.drawString("Enter Maidenhead Grid", 40, 92);
  M5.Display.drawRect(20, 160, 620, 55, TFT_BLACK); M5.Display.drawString("Enter Lat / Lon", 40, 172);
  M5.Display.drawRect(20, 240, 620, 55, TFT_BLACK); M5.Display.drawString("WiFi Configuration", 40, 252);
  M5.Display.drawRect(20, 320, 620, 55, TFT_BLACK); M5.Display.drawString("Auto Location via WiFi", 40, 332);
  M5.Display.drawRect(20, 400, 620, 55, TFT_BLACK); M5.Display.drawString("Set Time/Date (UTC)", 40, 412);

  M5.Display.drawRect(720, 480, 200, 55, TFT_BLACK); M5.Display.drawString("Back", 760, 498);
  M5.Display.display();
}

void drawGridInputScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Enter Maidenhead Grid (4 or 6 chars)", 20, 20);
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
  M5.Display.drawString("Enter Lat,Lon (e.g. 40.7128,-74.0060)", 20, 20);
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

void drawTimeInputScreen() {
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("Set UTC Time/Date", 20, 20);
  M5.Display.drawString("Format: YYYY-MM-DD HH:MM:SS or T", 20, 50);
  M5.Display.drawString(inputBuffer.c_str(), 40, 80);

  const char* timeKeys[] = {
    "1", "2", "3", "4", "5",
    "6", "7", "8", "9", "0",
    "-", "T", ":", "Del", "Clr"
  };
  for (int i = 0; i < 15; i++) {
    int col = i % 5;
    int row = i / 5;
    int x = 40 + col * 105;
    int y = 150 + row * 55;
    int w = 95;
    int h = 48;
    M5.Display.drawRect(x, y, w, h, TFT_BLACK);
    const char* label = timeKeys[i];
    if (strcmp(label, "Del") == 0 || strcmp(label, "Clr") == 0) {
      M5.Display.drawString(label, x + 15, y + 14);
    } else {
      M5.Display.drawString(label, x + 30, y + 14);
    }
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
    else if (wasTouched(720, 180, 200, 55)) { currentSatPage = 0; currentScreen = SAT_SELECT; drawSatSelectScreen(); }
    else if (wasTouched(720, 260, 200, 55)) { currentScreen = SETUP_MENU; drawSetupMenu(); }
  }
  else if (currentScreen == SAT_SELECT) {
    if (wasTouched(720, 470, 200, 55)) { currentScreen = MAIN; drawMainScreen(); return; }

    // Page navigation
    if (wasTouched(720, 130, 200, 55)) { // Prev
      if (currentSatPage > 0) {
        currentSatPage--;
        drawSatSelectScreen();
        return;
      }
    }
    if (wasTouched(720, 200, 200, 55)) { // Next
      int totalPages = (satCount + satsPerPage - 1) / satsPerPage;
      if (currentSatPage < totalPages - 1) {
        currentSatPage++;
        drawSatSelectScreen();
        return;
      }
    }

    if (wasTouched(720, 280, 200, 55)) { // Update GP - force redownload of AMSAT daily-bulletin.json
      forceTLEUpdate = true;
      updateData();
      drawSatSelectScreen();
      return;
    }

    // Sat list items on current page
    int startIdx = currentSatPage * satsPerPage;
    int numOnPage = (satsPerPage < (satCount - startIdx) ? satsPerPage : (satCount - startIdx));
    for (int j = 0; j < numOnPage; j++) {
      int i = startIdx + j;
      int y = 60 + j * 40;
      if (wasTouched(20, y, 620, 36)) {
        selectedName = satList[i].name;
        selectedNorad = satList[i].norad;
        saveConfig();

        // Clear old TLE so fetchTLE() will extract fresh one for the newly selected satellite
        currentTLE1[0] = '\0';
        currentTLE2[0] = '\0';

        currentScreen = MAIN;
        updateData();
        drawMainScreen();
        return;
      }
    }
  }
  else if (currentScreen == SETUP_MENU) {
    if (wasTouched(720, 480, 200, 55)) { currentScreen = MAIN; drawMainScreen(); return; }

    if (wasTouched(20, 80, 620, 55)) { currentScreen = GRID_INPUT; inputBuffer = ""; drawGridInputScreen(); }
    else if (wasTouched(20, 160, 620, 55)) { currentScreen = LATLON_INPUT; inputBuffer = ""; drawLatLonInputScreen(); }
    else if (wasTouched(20, 240, 620, 55)) { openSetupPortal(); }
    else if (wasTouched(20, 320, 620, 55)) { autoLocateViaWiFi(); currentScreen = MAIN; drawMainScreen(); }
    else if (wasTouched(20, 400, 620, 55)) { currentScreen = TIME_INPUT; inputBuffer = ""; drawTimeInputScreen(); }
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
  else if (currentScreen == TIME_INPUT) {
    if (wasTouched(720, 160, 200, 55)) { // Done
      int y, m, d, h, mi, s = 0;
      int n = sscanf(inputBuffer.c_str(), "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &mi, &s);
      if (n < 6) {
        n = sscanf(inputBuffer.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &mi, &s);
      }
      if (n < 6) {
        n = sscanf(inputBuffer.c_str(), "%d-%d-%d %d:%d", &y, &m, &d, &h, &mi);
        if (n == 5) s = 0;
      }
      bool valid = false;
      if (n >= 5 && y >= 2020 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31 &&
          h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59) {
        setSystemTime(y, m, d, h, mi, s);
        statusMsg = "Time set successfully (UTC)";
        valid = true;
      } else {
        statusMsg = "Invalid format. Use YYYY-MM-DD HH:MM:SS";
      }
      currentScreen = MAIN;
      updateData();
      drawMainScreen();
      return;
    }
    if (wasTouched(720, 240, 200, 55)) { // Back
      currentScreen = SETUP_MENU; drawSetupMenu(); return;
    }
    const char* timeKeys[] = {
      "1", "2", "3", "4", "5",
      "6", "7", "8", "9", "0",
      "-", "T", ":", "Del", "Clr"
    };
    for (int i = 0; i < 15; i++) {
      int col = i % 5;
      int row = i / 5;
      int x = 40 + col * 105;
      int y = 150 + row * 55;
      int w = 95;
      int h = 48;
      if (wasTouched(x, y, w, h)) {
        String k = timeKeys[i];
        if (k == "Del") {
          if (inputBuffer.length() > 0) inputBuffer.remove(inputBuffer.length() - 1);
        } else if (k == "Clr") {
          inputBuffer = "";
        } else {
          if (inputBuffer.length() < 19) inputBuffer += k;
        }
        drawTimeInputScreen();
        return;
      }
    }
  }
}

void openSetupPortal() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  M5.Display.clearDisplay();
  M5.Display.setTextSize(2);
  M5.Display.drawString("WiFi Setup Portal", 40, 80);
  M5.Display.drawString("Connect to: M5PaperS3-Setup", 20, 140);
  M5.Display.drawString("Then open 192.168.4.1 in browser", 10, 180);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Portal will timeout after 5 minutes", 20, 220);
  M5.Display.display();

  wm.startConfigPortal("M5PaperS3-Setup");

  // Portal closed (credentials saved or timeout)
  WiFi.begin();           // reconnect with new creds
  delay(800);             // give WiFi a moment to connect

  statusMsg = "WiFi credentials saved";
  // Bonus: automatically set location from public IP now that we have internet
  if (WiFi.status() == WL_CONNECTED) {
    autoLocateViaWiFi();
  }
  currentScreen = MAIN;
  drawMainScreen();
}

void setup() {
  M5.begin();
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_BLACK);

  if (!LittleFS.begin()) {
    LittleFS.format();  // Attempt to recover corrupted or unformatted LittleFS
    if (!LittleFS.begin()) {
      // LittleFS mount failed; downloads will still work but no offline cache
      statusMsg = "LittleFS mount failed";
    }
  }

  loadConfig();
  WiFi.begin();
  configTime(0, 0, "pool.ntp.org");

  updateData();
  drawMainScreen();
}

void loop() {
  M5.update();
  handleTouch();

  if (currentScreen == MAIN) {
    unsigned long currentInterval = (sat.satEl > 0) ? 15000 : 60000;

    if (millis() - lastUpdate > currentInterval) {
      updateData();
      drawMainScreen();
    }
  }
  delay(50);
}
