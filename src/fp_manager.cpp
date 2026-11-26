// ---------------------------------------------------------------------------
//  Служебный режим: управление отпечатками R503-S
//
//  Попасть сюда можно только с удержанной правой кнопкой при загрузке и
//  правильным PIN-кодом (см. fpManagerRun). Выход — только перезагрузка.
//
//  * запись новых пальцев (до 127) во флэш датчика
//  * человекочитаемые имена шаблонов (NVS)
//  * проверка пальца: вердикт, ID и достоверность
//  * список и удаление шаблонов
//
//  Модуль использует общие объекты дисплея и тача, созданные в
//  T-DISPALY_passwordBox.cpp (см. app_shared.h).
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <CST816_TouchLib.h>
#include <Adafruit_Fingerprint.h>

#include "app_shared.h"
#include "config.h"
#include "fp_manager.h"

/* ----------------------------------------------------------------- palette */
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static constexpr uint16_t C_BG     = rgb565(9, 12, 20);
static constexpr uint16_t C_PANEL  = rgb565(24, 30, 44);
static constexpr uint16_t C_PANEL2 = rgb565(44, 54, 76);
static constexpr uint16_t C_ACCENT = rgb565(0, 196, 255);
static constexpr uint16_t C_TEXT   = rgb565(234, 240, 250);
static constexpr uint16_t C_MUTED  = rgb565(120, 134, 158);
static constexpr uint16_t C_OK     = rgb565(40, 220, 130);
static constexpr uint16_t C_ERR    = rgb565(255, 76, 76);
static constexpr uint16_t C_WARN   = rgb565(255, 190, 60);

/* ----------------------------------------------------------------- objects */
static Preferences prefs;
static Adafruit_Fingerprint finger(&Serial2);

/* ------------------------------------------------------------------- types */
struct TouchPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool valid = false;
};
struct Btn {
  int16_t x, y, w, h;
};

enum Screen : uint8_t { SCR_HOME, SCR_PICK, SCR_ENROLL, SCR_VERIFY, SCR_LIST, SCR_INFO };
enum VerifyState : uint8_t { V_IDLE, V_OK, V_NO };
enum WaitResult : uint8_t { WAIT_OK, WAIT_TIMEOUT, WAIT_CANCEL, WAIT_COMM };

/* ----------------------------------------------------------------- buttons */
static const Btn btnAdd      = {10,  66, 150, 42};
static const Btn btnCheck    = {10, 114, 150, 42};
static const Btn btnList     = {10, 162, 150, 42};
static const Btn btnInfo     = {10, 210, 150, 42};
static const Btn btnBack     = { 6,   5,  26, 24};
static const Btn btnBackWide = {10, 280, 150, 32};
static const Btn btnCancel   = {35, 258, 100, 36};
static const Btn btnPrev     = { 6, 236,  34, 30};
static const Btn btnNext     = {130, 236,  34, 30};
static const Btn btnClear    = { 6, 272, 158, 30};
static const Btn btnYes      = {18, 166,  62, 32};
static const Btn btnNo       = {90, 166,  62, 32};

static const char *FINGER_NAMES[8] = {
    "THUMB R", "INDEX R", "MIDDLE R", "THUMB L",
    "INDEX L", "MIDDLE L", "RING F",  "PINKY F"};

static Btn nameBtn(uint8_t i) {
  return Btn{(int16_t)(6 + (i % 2) * 82), (int16_t)(56 + (i / 2) * 44), 78, 38};
}

/* ------------------------------------------------------------------- state */
Screen screen = SCR_HOME;

bool sensorOnline = false;
bool ledOk = true;
uint32_t lastSensorRetry = 0;
uint16_t storedCount = 0;
uint16_t maxId = MAX_FP_ID;
uint32_t lastScanMs = 0;

uint8_t occ[16] = {0};                       // 128-bit "template used" bitmap
uint16_t usedIds[MAX_FP_ID];
uint16_t usedN = 0;

VerifyState vState = V_IDLE;
uint32_t vStateUntil = 0;
uint16_t vLastId = 0;
uint16_t vLastConf = 0;
bool vLastOk = false;
bool vLowScore = false;      // last result was a match the module scored too low
uint8_t vSearch = 0;         // raw status byte of the last 1:N search
uint32_t vChecks = 0;
uint32_t vMatches = 0;

uint8_t listPage = 0;
uint8_t lastPct = 255;

int16_t lastTouchX = 0;
int16_t lastTouchY = 0;
bool touchSeen = false;

/* --- R503 WAKEUP line (blue wire) --------------------------------------- */
static const int16_t WAKE_ROW_HOME = 54;    // y of the live row on the home screen
static const int16_t WAKE_ROW_CHECK = 262;  // y of the live row on the check screen

bool wakeRaw = false;         // raw (still debouncing) level, true = finger
uint32_t wakeRawSince = 0;
bool wakeFinger = false;      // committed state
uint32_t wakePulses = 0;      // number of placements (LOW edges)
uint32_t wakeEdgeMs = 0;      // millis when the current pulse started
uint32_t wakeHoldMs = 0;      // length of the last completed pulse
bool wakeDirty = false;       // the wake indicator needs a redraw

/* --- capture statistics (shown on the INFO screen) ---------------------- */
uint16_t capOk = 0;           // image -> template conversions that succeeded
uint16_t capFail = 0;         // ... and that failed
uint8_t lastCapErr = 0;       // last sensor error code during enrolment

/* ------------------------------------------------------- forward declarations */
void drawHome();
void drawPick();
void drawList();
void drawInfo();
void drawVerifyScreen();
void renderVerify();
void drawSplash(const String &msg);
void gotoHome();
void startVerify();
void enrollFlow(const String &label);
String nameOf(uint16_t id);
String cnt(uint32_t v);
void sensorHealth();
void reconcile(bool showSplashUi);
void renderWakeRow(int16_t y);
void renderWakeInfo();
void fpFlushRx();

/* ------------------------------------------------------------ small helpers */
bool inside(const Btn &b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
bool inside(const Btn &b, const TouchPoint &p) {
  return p.valid && inside(b, p.x, p.y);
}

void textL(const String &s, int16_t x, int16_t y, uint8_t font, uint16_t fg,
           uint16_t bg) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y, font);
}

void textRT(const String &s, int16_t xr, int16_t y, uint8_t font, uint16_t fg,
            uint16_t bg) {
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(s, xr, y, font);
}

void textC(const String &s, int16_t cx, int16_t cy, uint8_t font, uint16_t fg,
           uint16_t bg) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(s, cx, cy, font);
}

void drawButton(const Btn &b, const String &label, uint16_t fill, uint16_t border,
                uint16_t fg, uint8_t font) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, fill);
  if (border) tft.drawRoundRect(b.x, b.y, b.w, b.h, 8, border);
  textC(label, b.x + b.w / 2, b.y + b.h / 2, font, fg, fill);
}

void drawFingerIcon(int16_t cx, int16_t cy, uint16_t color) {
  tft.drawCircle(cx, cy, 30, color);
  tft.drawCircle(cx, cy, 23, color);
  tft.drawCircle(cx, cy, 16, color);
  tft.fillCircle(cx, cy, 6, color);
}

void drawHeader(const char *title, bool withBack) {
  tft.fillRect(0, 0, SCREEN_W, 34, C_PANEL);
  tft.drawFastHLine(0, 34, SCREEN_W, C_PANEL2);
  if (withBack) drawButton(btnBack, "<", C_PANEL2, C_PANEL, C_ACCENT, 2);
  textL(title, withBack ? 40 : 10, 9, 2, C_TEXT, C_PANEL);
}

/* -------------------------------------------------------------------- touch */
TouchPoint pollTouch() {
  TouchPoint p;
  oTouch.control();
  if (!oTouch.hadTouch()) return p;

  // The library swaps the panel axes: first value is vertical (0..319),
  // second is horizontal (0..169).
  int vertical = 0, horizontal = 0;
  oTouch.getLastTouchPosition(vertical, horizontal);

  int16_t sx = (int16_t)horizontal;
  int16_t sy = (int16_t)vertical;

#if TOUCH_SWAP_XY
  int16_t t = sx;
  sx = sy;
  sy = t;
#endif
#if TOUCH_INVERT_X
  sx = (int16_t)(SCREEN_W - 1 - sx);
#endif
#if TOUCH_INVERT_Y
  sy = (int16_t)(SCREEN_H - 1 - sy);
#endif

  if (sx < 0) sx = 0;
  if (sx > SCREEN_W - 1) sx = SCREEN_W - 1;
  if (sy < 0) sy = 0;
  if (sy > SCREEN_H - 1) sy = SCREEN_H - 1;

  p.x = sx;
  p.y = sy;
  p.valid = true;
  lastTouchX = sx;
  lastTouchY = sy;
  touchSeen = true;
#if DEBUG_TOUCH
  Serial.printf("[touch] %d,%d\n", sx, sy);
#endif
  return p;
}

/* --------------------------------------------------------- R503 WAKEUP line */
// Blue wire of the R503: idle HIGH, LOW while a finger touches the sensor.
bool fpWakeActive() {
  int level = digitalRead(PIN_FP_WAKE);
#if FP_WAKE_ACTIVE_LOW
  return level == LOW;
#else
  return level == HIGH;
#endif
}

// 12 ms of stable level filters contact chatter but still feels instant.
void pollWake() {
  const uint32_t DEBOUNCE_MS = 12;
  bool raw = fpWakeActive();
  uint32_t now = millis();

  if (raw != wakeRaw) {   // level moved: restart the stability window
    wakeRaw = raw;
    wakeRawSince = now;
    return;
  }
  if (now - wakeRawSince < DEBOUNCE_MS) return;
  if (raw == wakeFinger) return;      // already committed

  wakeFinger = raw;
  wakeDirty = true;
  if (raw) {
    wakeEdgeMs = now;
    if (wakePulses < 999999UL) wakePulses++;
    Serial.printf("[wake] finger on sensor (#%lu)\n", (unsigned long)wakePulses);
  } else {
    if (wakeEdgeMs) wakeHoldMs = now - wakeEdgeMs;
    Serial.printf("[wake] released, held %lu ms\n", (unsigned long)wakeHoldMs);
  }
}

// One-line live indicator, used on the home and check screens.
void renderWakeRow(int16_t y) {
  tft.fillRect(6, y - 2, 158, 12, C_BG);
  uint16_t c = wakeFinger ? C_OK : C_MUTED;
  tft.fillCircle(12, y + 3, 3, c);
  textL("WAKE", 20, y, 1, C_MUTED, C_BG);
  textL(wakeFinger ? "FINGER" : "-----", 52, y, 1, c, C_BG);
  textRT("#" + cnt(wakePulses), 160, y, 1, C_MUTED, C_BG);
}

// Two-line block for the INFO screen: live state, pulse count, last hold time.
void renderWakeInfo() {
  tft.fillRect(6, 106, 158, 34, C_BG);
  uint16_t c = wakeFinger ? C_OK : C_MUTED;

  textL("WAKE", 10, 110, 1, C_MUTED, C_BG);
  tft.fillCircle(48, 114, 3, c);
  textL(wakeFinger ? "FINGER" : "-----", 56, 110, 1, c, C_BG);
  textRT("GPIO " + String(PIN_FP_WAKE), 160, 110, 1, C_MUTED, C_BG);

  textL("PULSES " + cnt(wakePulses), 10, 126, 1, C_MUTED, C_BG);
  char b[24];
  if (wakeHoldMs)
    snprintf(b, sizeof(b), "LAST %lu ms", (unsigned long)wakeHoldMs);
  else
    snprintf(b, sizeof(b), "NO PULSE YET");
  textRT(b, 160, 126, 1, C_MUTED, C_BG);
}

/* --- live capture probe (INFO screen) ----------------------------------- */
// Shows what the sensor actually answers while a finger is held: the getImage
// result and, if an image arrived, the image2Tz result. This separates
// "sensor does not see the finger" from "image too poor to convert".
uint8_t probeImg = 0;
uint8_t probeTz = 0;
bool probeDone = false;
uint32_t probeMs = 0;

String hex2(uint8_t v) {
  char b[8];
  snprintf(b, sizeof(b), "0x%02X", v);
  return String(b);
}

void renderProbe() {
  tft.fillRect(6, 152, 158, 12, C_BG);
  if (!sensorOnline) {
    textL("PROBE     SENSOR OFFLINE", 10, 154, 1, C_ERR, C_BG);
    return;
  }
  if (!probeDone) {
    textL("PROBE     --", 10, 154, 1, C_MUTED, C_BG);
    return;
  }

  uint16_t col = C_MUTED;
  if (probeImg == FINGERPRINT_OK && probeTz == FINGERPRINT_OK) col = C_OK;
  else if (probeImg == FINGERPRINT_OK) col = C_WARN;

  String tz = (probeImg == FINGERPRINT_OK) ? hex2(probeTz) : String("--");
  textL("PROBE IMG " + hex2(probeImg) + " TZ " + tz, 10, 154, 1, col, C_BG);
}

void probeTask() {
  if (screen != SCR_INFO) return;
  if (millis() - probeMs < 700) return;
  probeMs = millis();

  if (!sensorOnline) {
    probeDone = false;
    renderProbe();
    return;
  }

  fpFlushRx();
  probeImg = finger.getImage();
  probeTz = 0xFF;
  if (probeImg == FINGERPRINT_OK) probeTz = finger.image2Tz(1);
  probeDone = true;
  Serial.printf("[probe] img=0x%02X tz=0x%02X\n", probeImg, probeTz);
  renderProbe();
}

/* ---------------------------------------------------------------  database */
bool isUsed(uint16_t id) {
  if (id < 1 || id > MAX_FP_ID) return false;
  uint16_t bit = id - 1;
  return (occ[bit >> 3] >> (bit & 7)) & 1;
}

void setUsed(uint16_t id, bool used) {
  if (id < 1 || id > MAX_FP_ID) return;
  uint16_t bit = id - 1;
  if (used)
    occ[bit >> 3] |= (uint8_t)(1u << (bit & 7));
  else
    occ[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
}

void saveOcc() { prefs.putBytes("occ", occ, sizeof(occ)); }

void loadStore() {
  prefs.begin("fpdb", false);
  memset(occ, 0, sizeof(occ));
  if (prefs.getBytesLength("occ") == sizeof(occ)) prefs.getBytes("occ", occ, sizeof(occ));
}

String nameOf(uint16_t id) {
  String key = "n" + String(id);
  String v = prefs.getString(key.c_str(), "");
  if (v.length()) return v;
  return "FINGER " + String(id);
}

void saveName(uint16_t id, const String &name) {
  String key = "n" + String(id);
  prefs.putString(key.c_str(), name);
}

void collectUsed() {
  usedN = 0;
  for (uint16_t id = 1; id <= maxId && usedN < MAX_FP_ID; ++id)
    if (isUsed(id)) usedIds[usedN++] = id;
}

uint16_t allocateId() {
  uint16_t start = prefs.getUShort("next", 1);
  if (start < 1 || start > maxId) start = 1;
  for (uint16_t i = 0; i < maxId; ++i) {
    uint16_t id = start + i;
    if (id > maxId) id -= maxId;
    if (!isUsed(id)) {
      prefs.putUShort("next", (uint16_t)(id >= maxId ? 1 : id + 1));
      return id;
    }
  }
  return 0;
}

String errText(uint8_t code) {
  char b[16];
  snprintf(b, sizeof(b), "CODE 0x%02X", code);
  return String(b);
}

// keeps the counters short enough for the 170 px wide status line
String cnt(uint32_t v) { return v > 9999 ? String("9999+") : String(v); }

/* ---------------------------------------------------------------- sensor IO */
void refreshCount() {
  if (!sensorOnline) return;
  if (finger.getTemplateCount() == FINGERPRINT_OK) storedCount = finger.templateCount;
}

// Short human hint for the sensor's error codes, used on screen and in logs.
String capHint(uint8_t c) {
  char b[24];
  switch (c) {
    case FINGERPRINT_IMAGEFAIL:
      snprintf(b, sizeof(b), "0x%02X HOLD STILL", c);
      break;
    case FINGERPRINT_IMAGEMESS:
      snprintf(b, sizeof(b), "0x%02X CLEAN / DRY", c);
      break;
    case FINGERPRINT_FEATUREFAIL:
      snprintf(b, sizeof(b), "0x%02X PRESS FLAT", c);
      break;
    case FINGERPRINT_INVALIDIMAGE:
      snprintf(b, sizeof(b), "0x%02X NO IMAGE", c);
      break;
    default:
      snprintf(b, sizeof(b), "CODE 0x%02X", c);
      break;
  }
  return String(b);
}

// Drop anything left in the UART RX buffer so a late/partial answer cannot be
// mistaken for the reply to the next command.
void fpFlushRx() {
  while (Serial2.available()) Serial2.read();
}

// Capture an image and convert it into character file `slot`.
//
// Notes from real hardware:
//  * the first frame right after contact is often blurred, so settle first;
//  * the module also answers "no finger" (0x02) for a too-faint image, so a
//    single miss does not mean the finger was removed - only several in a row do.
uint8_t captureToSlot(uint8_t slot) {
  uint8_t lastErr = FINGERPRINT_IMAGEFAIL;
  uint8_t misses = 0;

  for (uint8_t attempt = 1; attempt <= 6; ++attempt) {
    delay(250);                 // let the finger come to rest
    fpFlushRx();

    uint8_t r = finger.getImage();
    if (r == FINGERPRINT_PACKETRECIEVEERR || r == FINGERPRINT_BADPACKET) return r;

    if (r == FINGERPRINT_NOFINGER) {
      if (++misses >= 4) return FINGERPRINT_NOFINGER;   // finger really gone
      delay(100);
      continue;
    }
    misses = 0;

    if (r != FINGERPRINT_OK) {
      lastErr = r;
      delay(100);
      continue;
    }

    uint8_t s = finger.image2Tz(slot);
    if (s == FINGERPRINT_OK) {
      if (capOk < 60000) capOk++;
      Serial.printf("[cap] slot %u ok (attempt %u)\n", slot, attempt);
      return FINGERPRINT_OK;
    }

    lastErr = s;
    Serial.printf("[cap] slot %u attempt %u -> 0x%02X\n", slot, attempt, s);
    if (s == FINGERPRINT_PACKETRECIEVEERR || s == FINGERPRINT_BADPACKET) return s;
    delay(100);
  }

  if (capFail < 60000) capFail++;
  lastCapErr = lastErr;
  return lastErr;
}

// 1:N search with a fallback.
//
// Some clone firmwares do not implement the high-speed search (0x1B) and answer
// it with "OK" and an empty payload; the library then reads leftover bytes and
// reports junk like id=0 / score=0. Others only implement 0x1B. So try the
// classic 0x04 first and fall back to 0x1B (and back) until one gives a usable
// answer. 0xFFFF as id or 0 as score always means "no match".
uint8_t searchFinger(uint16_t &outId, uint16_t &outConf) {
  uint8_t s = finger.fingerSearch(1);
  uint16_t id = finger.fingerID;
  uint16_t conf = finger.confidence;
  Serial.printf("[search] 0x04 -> 0x%02X id=%u conf=%u\n", s, id, conf);

  bool usable = (s == FINGERPRINT_OK && id != 0xFFFF && conf > 0) ||
                (s == FINGERPRINT_NOTFOUND);

  if (!usable) {
    uint8_t f = finger.fingerFastSearch();
    Serial.printf("[search] 0x1B -> 0x%02X id=%u conf=%u\n", f, finger.fingerID,
                  finger.confidence);
    if (f == FINGERPRINT_OK && finger.fingerID != 0xFFFF && finger.confidence > 0) {
      s = f;
      id = finger.fingerID;
      conf = finger.confidence;
    } else if (f == FINGERPRINT_NOTFOUND) {
      s = FINGERPRINT_NOTFOUND;
      id = 0xFFFF;
      conf = 0;
    }
  }

  outId = id;
  outConf = conf;
  return s;
}

void ledSet(uint8_t color, uint8_t func) {
#if USE_AURA_LED
  if (!sensorOnline || !ledOk) return;
  // R503 AuraLedConfig: func, start colour, end colour, cycles
  if (finger.LEDcontrol(func, color, color, 0x00) != FINGERPRINT_OK) ledOk = false;
#else
  (void)color;
  (void)func;
#endif
}

void sensorLost(const char *where) {
  if (sensorOnline) Serial.printf("[fp] sensor lost (%s)\n", where);
  sensorOnline = false;
  lastSensorRetry = millis();
}

void sensorHealth() {
  if (sensorOnline) return;
  if (millis() - lastSensorRetry < 3000) return;
  lastSensorRetry = millis();

  if (!finger.verifyPassword()) return;

  sensorOnline = true;
  ledOk = true;
  if (finger.getParameters() == FINGERPRINT_OK) {
    if (finger.capacity > 0 && finger.capacity < MAX_FP_ID) maxId = finger.capacity;
  }
  finger.capacity = maxId;   // search range of the 0x04 command
  refreshCount();
  ledSet(LED_IDLE, FINGERPRINT_LED_ON);
  Serial.printf("[fp] sensor online, %u templates, capacity %u\n", storedCount,
                finger.capacity);
  reconcile(false);

  if (screen == SCR_HOME) drawHome();
  else if (screen == SCR_VERIFY) renderVerify();
  else if (screen == SCR_INFO) drawInfo();
}

void reconcile(bool showSplashUi = true) {
  uint16_t bits = 0;
  for (size_t i = 0; i < sizeof(occ); ++i) bits += (uint16_t)__builtin_popcount(occ[i]);
  if (bits == storedCount) return;

  Serial.printf("[db] registry=%u sensor=%u, rebuilding\n", bits, storedCount);
  if (showSplashUi) drawSplash("SYNC STORAGE...");
  memset(occ, 0, sizeof(occ));
  for (uint16_t id = 1; id <= maxId; ++id) {
    if (finger.loadModel(id) == FINGERPRINT_OK) setUsed(id, true);
  }
  saveOcc();
  refreshCount();
}

bool deleteId(uint16_t id) {
  if (finger.deleteModel(id) != FINGERPRINT_OK) return false;
  setUsed(id, false);
  saveOcc();
  String key = "n" + String(id);
  prefs.remove(key.c_str());
  refreshCount();
  Serial.printf("[db] deleted id=%u\n", id);
  return true;
}

/* ------------------------------------------------------------------ screens */
void drawSplash(const String &msg) {
  tft.fillScreen(C_BG);
  drawFingerIcon(SCREEN_W / 2, 118, C_ACCENT);
  textC(APP_NAME, SCREEN_W / 2, 176, 4, C_TEXT, C_BG);
  textC("FINGERPRINT MANAGER", SCREEN_W / 2, 202, 1, C_MUTED, C_BG);
  textC(msg, SCREEN_W / 2, 262, 1, C_ACCENT, C_BG);
}

void drawHome() {
  collectUsed();
  tft.fillScreen(C_BG);

  textL("FINGERPRINT", 10, 9, 2, C_ACCENT, C_BG);

  textL(sensorOnline ? "SENSOR OK" : "SENSOR OFFLINE", 10, 40, 1,
        sensorOnline ? C_OK : C_ERR, C_BG);
  textRT(String(usedN) + " SAVED", 160, 40, 1, C_MUTED, C_BG);

  renderWakeRow(WAKE_ROW_HOME);

  drawButton(btnAdd, "ADD FINGER", C_PANEL, C_PANEL2, C_TEXT, 2);
  drawButton(btnCheck, "CHECK FINGER", C_PANEL, C_PANEL2, C_TEXT, 2);
  drawButton(btnList, "STORED LIST", C_PANEL, C_PANEL2, C_TEXT, 2);
  drawButton(btnInfo, "INFO / TEST", C_PANEL, C_PANEL2, C_ACCENT, 2);

  tft.drawFastHLine(10, 258, 150, C_PANEL);
  textL("LAST CHECK", 10, 264, 1, C_MUTED, C_BG);

  if (vChecks == 0) {
    textL("NONE YET", 10, 276, 2, C_MUTED, C_BG);
  } else if (vLastOk) {
    textL("ID " + String(vLastId), 10, 274, 2, C_OK, C_BG);
    textRT("CONF " + String(vLastConf), 160, 280, 1, C_MUTED, C_BG);
  } else {
    textL("NO MATCH", 10, 274, 2, C_ERR, C_BG);
  }
  textL("CHECKS " + cnt(vChecks) + "   OK " + cnt(vMatches), 10, 298, 1,
        C_MUTED, C_BG);
  textC("REBOOT TO EXIT", SCREEN_W / 2, 312, 1, C_MUTED, C_BG);
}

void drawPick() {
  tft.fillScreen(C_BG);
  drawHeader("NEW FINGER", true);
  textL("Which finger is it?", 10, 42, 1, C_MUTED, C_BG);
  for (uint8_t i = 0; i < 8; ++i)
    drawButton(nameBtn(i), FINGER_NAMES[i], C_PANEL, C_PANEL2, C_TEXT, 1);
  drawButton(btnCancel, "CANCEL", C_PANEL, C_PANEL2, C_MUTED, 2);
}

void drawInfo() {
  tft.fillScreen(C_BG);
  drawHeader("INFO", true);

  uint8_t y = 40;
  textL("DISPLAY   170x320", 10, y, 1, C_TEXT, C_BG);
  y += 14;
  textL("TOUCH     CST816S", 10, y, 1, C_TEXT, C_BG);
  y += 14;
  textL("SENSOR    " + String(sensorOnline ? "ONLINE" : "OFFLINE"), 10, y, 1,
        sensorOnline ? C_OK : C_ERR, C_BG);
  y += 14;
  textL("TEMPLATES " + String(storedCount) + " / " + String(maxId), 10, y, 1,
        C_TEXT, C_BG);
  y += 14;
  textL("UART TX" + String(PIN_FP_TX) + " RX" + String(PIN_FP_RX) + "  LV" +
        String(finger.security_level) + " MIN" + String(FP_MIN_CONFIDENCE), 10, y,
        1, C_MUTED, C_BG);

  renderWakeInfo();

  String cap = "CAP " + String(capOk) + " OK / " + String(capFail) + " ERR";
  if (lastCapErr) {
    char b[8];
    snprintf(b, sizeof(b), "  0x%02X", lastCapErr);
    cap += b;
  }
  textL(cap, 10, 142, 1, lastCapErr ? C_WARN : C_MUTED, C_BG);

  renderProbe();

  textL("TOUCH TEST", 10, 170, 1, C_ACCENT, C_BG);
  tft.drawRect(6, 182, 158, 68, C_PANEL2);

  if (touchSeen) {
    int16_t px = lastTouchX;
    int16_t py = lastTouchY;
    if (px < 7) px = 7;
    if (px > 162) px = 162;
    if (py < 183) py = 183;
    if (py > 248) py = 248;
    tft.drawFastHLine(7, py, 156, C_ACCENT);
    tft.drawFastVLine(px, 183, 66, C_ACCENT);
    textL("X " + String(lastTouchX) + "  Y " + String(lastTouchY), 11, 186, 1,
          C_TEXT, C_BG);
  } else {
    textC("NO TOUCH YET", SCREEN_W / 2, 216, 1, C_MUTED, C_BG);
  }

  drawButton(btnBackWide, "BACK", C_PANEL, C_PANEL2, C_TEXT, 2);
}

/* --------------------------------------------------------- enrolment screen */
void drawSteps(uint8_t done) {
  for (uint8_t i = 0; i < 2; ++i) {
    uint16_t c = (i < done) ? C_ACCENT : C_PANEL2;
    tft.fillRoundRect(45 + i * 40, 240, 40, 6, 3, c);
  }
}

void resetProgress() {
  lastPct = 255;
  tft.fillRect(11, 307, 148, 4, C_BG);
}

void setProgress(uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct == lastPct) return;
  lastPct = pct;
  tft.fillRect(11, 307, 148, 4, C_BG);
  if (pct) tft.fillRect(11, 307, (148 * pct) / 100, 4, C_ACCENT);
}

void setEnrollMsg(const String &msg, const String &sub) {
  tft.fillRect(0, 176, SCREEN_W, 52, C_BG);
  textC(msg, SCREEN_W / 2, 188, 2, C_TEXT, C_BG);
  textC(sub, SCREEN_W / 2, 210, 1, C_MUTED, C_BG);
}

void drawEnrollScreen(const String &label) {
  tft.fillScreen(C_BG);
  drawHeader("ADD FINGER", false);
  textC(label, SCREEN_W / 2, 54, 2, C_ACCENT, C_BG);
  drawFingerIcon(SCREEN_W / 2, 124, C_PANEL2);
  tft.drawRect(10, 306, 150, 6, C_PANEL2);
  drawSteps(0);
  drawButton(btnCancel, "CANCEL", C_PANEL, C_PANEL2, C_MUTED, 2);
  resetProgress();
}

void retryMsg(const String &msg, const String &sub) {
  ledSet(LED_DENY, FINGERPRINT_LED_FLASHING);
  setEnrollMsg(msg, sub);
  delay(1200);
  ledSet(LED_ENROLL, FINGERPRINT_LED_BREATHING);
  resetProgress();
}

void failScreen(const String &msg, const String &sub) {
  ledSet(LED_DENY, FINGERPRINT_LED_ON);
  setEnrollMsg(msg, sub);
  delay(1800);
  ledSet(LED_IDLE, FINGERPRINT_LED_ON);
  gotoHome();
}

void gotoHome() {
  screen = SCR_HOME;
  drawHome();
}

/* -------------------------------------------------------------- check screen */
void drawVerifyScreen() {
  tft.fillScreen(C_BG);
  drawHeader("CHECK FINGER", true);
  drawButton(btnBackWide, "BACK", C_PANEL, C_PANEL2, C_TEXT, 2);
  renderVerify();
}

void renderVerify() {
  tft.fillRect(0, 38, SCREEN_W, 238, C_BG);

  uint16_t ring = C_ACCENT;
  String verdict = "READY";
  uint16_t vcol = C_MUTED;
  String l1 = "PLACE FINGER";
  String l2 = "ON THE SENSOR";

  if (!sensorOnline) {
    ring = C_ERR;
    verdict = "OFFLINE";
    vcol = C_ERR;
    l1 = "SENSOR NOT FOUND";
    l2 = "CHECK THE WIRING";
  } else if (vState == V_OK) {
    ring = C_OK;
    verdict = "MATCH";
    vcol = C_OK;
    l1 = "ID " + String(vLastId) + "  " + nameOf(vLastId);
    l2 = "CONFIDENCE " + String(vLastConf);
  } else if (vState == V_NO) {
    ring = C_ERR;
    verdict = "DENIED";
    vcol = C_ERR;
    if (vLowScore) {
      l1 = "WEAK MATCH";
      l2 = "ID " + String(vLastId) + " SC " + String(vLastConf) + " MIN " +
           String(FP_MIN_CONFIDENCE);
    } else {
      l1 = "UNKNOWN FINGER";
      l2 = "SENSOR " + hex2(vSearch);
    }
  }

  drawFingerIcon(SCREEN_W / 2, 106, ring);
  textC(verdict, SCREEN_W / 2, 170, 4, vcol, C_BG);
  textC(l1, SCREEN_W / 2, 202, 2, C_TEXT, C_BG);
  textC(l2, SCREEN_W / 2, 224, 2, C_MUTED, C_BG);
  textC("CHECKS " + cnt(vChecks) + "   OK " + cnt(vMatches), SCREEN_W / 2,
        250, 1, C_MUTED, C_BG);
  renderWakeRow(WAKE_ROW_CHECK);
}

void startVerify() {
  screen = SCR_VERIFY;
  vState = V_IDLE;
  vStateUntil = 0;
  lastScanMs = 0;
  drawVerifyScreen();
  ledSet(LED_IDLE, FINGERPRINT_LED_BREATHING);
}

void verifyPoll() {
  if (!sensorOnline) {
    sensorHealth();
    return;
  }

  if (vState != V_IDLE) {
    // keep the verdict on screen until the finger is taken away, so that one
    // touch cannot produce a burst of matches
    if (millis() < vStateUntil) return;
    if (millis() - lastScanMs < SCAN_INTERVAL) return;
    lastScanMs = millis();

    uint8_t r = finger.getImage();
    if (r == FINGERPRINT_PACKETRECIEVEERR || r == FINGERPRINT_BADPACKET) {
      sensorLost("check/lift");
      renderVerify();
      return;
    }
    if (r == FINGERPRINT_NOFINGER) {
      vState = V_IDLE;
      ledSet(LED_IDLE, FINGERPRINT_LED_BREATHING);
      renderVerify();
    } else {
      vStateUntil = millis() + 300;
    }
    return;
  }

  if (millis() - lastScanMs < SCAN_INTERVAL) return;
  lastScanMs = millis();

  uint8_t r = finger.getImage();
  if (r == FINGERPRINT_NOFINGER) return;
  if (r == FINGERPRINT_PACKETRECIEVEERR || r == FINGERPRINT_BADPACKET) {
    sensorLost("check/getImage");
    renderVerify();
    return;
  }
  if (r != FINGERPRINT_OK) return;

  if (finger.image2Tz(1) != FINGERPRINT_OK) return;

  uint16_t id = 0xFFFF;
  uint16_t conf = 0;
  uint8_t s = searchFinger(id, conf);
  vSearch = s;

  if (s == FINGERPRINT_OK && id != 0xFFFF && conf > 0) {
    vChecks++;
    vLastId = id;
    vLastConf = conf;

    // The module (clones, or a lax security level) can report a match for
    // almost any finger, so a match is only accepted with a strong score on a
    // template that is actually registered here.
    if (conf < FP_MIN_CONFIDENCE || !isUsed(id)) {
      vLastOk = false;
      vLowScore = true;
      vState = V_NO;
      vStateUntil = millis() + 2200;
      ledSet(LED_DENY, FINGERPRINT_LED_FLASHING);
      Serial.printf("[check] weak match id=%u conf=%u (min %u) -> denied\n", id,
                    conf, FP_MIN_CONFIDENCE);
    } else {
      vMatches++;
      vLastOk = true;
      vLowScore = false;
      vState = V_OK;
      vStateUntil = millis() + 3000;
      ledSet(LED_OK, FINGERPRINT_LED_FLASHING);
      Serial.printf("[check] MATCH id=%u name=%s conf=%u\n", id,
                    nameOf(id).c_str(), conf);
    }
    renderVerify();
  } else if (s == FINGERPRINT_OK || s == FINGERPRINT_NOTFOUND) {
    // no usable match: NOTFOUND (0x09), or a clone answering OK with an empty
    // payload (id 0xFFFF / score 0)
    vChecks++;
    vLastOk = false;
    vLowScore = false;
    vLastId = id;
    vLastConf = conf;
    vState = V_NO;
    vStateUntil = millis() + 2000;
    ledSet(LED_DENY, FINGERPRINT_LED_FLASHING);
    Serial.printf("[check] NO MATCH (status 0x%02X id=%u conf=%u)\n", s, id, conf);
    renderVerify();
  } else if (s == FINGERPRINT_PACKETRECIEVEERR || s == FINGERPRINT_BADPACKET) {
    sensorLost("check/search");
    renderVerify();
  }
}

/* --------------------------------------------------------------- list screen */
void drawList() {
  collectUsed();
  tft.fillScreen(C_BG);
  drawHeader("STORED", true);
  textRT(String(usedN) + "/" + String(maxId), 160, 9, 2, C_MUTED, C_PANEL);

  if (usedN == 0) {
    textC("NO FINGERS", SCREEN_W / 2, 120, 2, C_MUTED, C_BG);
    textC("ADD ONE FIRST", SCREEN_W / 2, 144, 1, C_MUTED, C_BG);
    return;
  }

  const uint8_t PER = 5;
  uint8_t pages = (uint8_t)((usedN + PER - 1) / PER);
  if (listPage >= pages) listPage = pages - 1;

  for (uint8_t i = 0; i < PER; ++i) {
    uint16_t idx = (uint16_t)listPage * PER + i;
    if (idx >= usedN) break;
    uint16_t id = usedIds[idx];
    int16_t y = 42 + i * 38;
    tft.fillRoundRect(6, y, 158, 34, 6, C_PANEL);
    tft.drawRoundRect(6, y, 158, 34, 6, C_PANEL2);
    textL("ID " + String(id), 14, y + 9, 1, C_ACCENT, C_PANEL);
    textL(nameOf(id), 54, y + 8, 2, C_TEXT, C_PANEL);
  }

  if (pages > 1) {
    drawButton(btnPrev, "<", listPage ? C_PANEL : C_BG, C_PANEL2, C_ACCENT, 2);
    drawButton(btnNext, ">", (listPage + 1 < pages) ? C_PANEL : C_BG, C_PANEL2,
               C_ACCENT, 2);
    textC(String(listPage + 1) + "/" + String(pages), SCREEN_W / 2, 251, 1,
          C_MUTED, C_BG);
  }
  drawButton(btnClear, "CLEAR ALL", C_PANEL, C_PANEL2, C_ERR, 1);
}

bool askConfirm(const String &title, const String &detail) {
  tft.fillRoundRect(12, 96, 146, 116, 10, C_PANEL2);
  tft.drawRoundRect(12, 96, 146, 116, 10, C_ACCENT);
  textC(title, SCREEN_W / 2, 118, 2, C_TEXT, C_PANEL2);
  textC(detail, SCREEN_W / 2, 142, 1, C_MUTED, C_PANEL2);
  drawButton(btnYes, "YES", C_ERR, 0, TFT_WHITE, 2);
  drawButton(btnNo, "NO", C_PANEL, 0, C_TEXT, 2);

  for (;;) {
    TouchPoint p = pollTouch();
    if (p.valid) {
      if (inside(btnYes, p)) return true;
      if (inside(btnNo, p)) return false;
    }
    delay(8);
  }
}

void clearAll() {
  if (finger.emptyDatabase() != FINGERPRINT_OK) {
    sensorLost("clear");
    gotoHome();
    return;
  }
  prefs.clear();
  memset(occ, 0, sizeof(occ));
  saveOcc();
  prefs.putUShort("next", 1);
  usedN = 0;
  refreshCount();
  Serial.println("[db] database cleared");
  drawList();
}

/* ----------------------------------------------------------------- enrolment */
WaitResult waitFinger(uint32_t timeoutMs, const char *msg, const char *sub,
                      uint8_t step, bool wantFinger) {
  setEnrollMsg(msg, sub);
  drawSteps(step);
  resetProgress();

  const uint32_t t0 = millis();
  uint32_t lastPoll = 0;

  while (millis() - t0 < timeoutMs) {
    TouchPoint p = pollTouch();
    if (p.valid && inside(btnCancel, p)) return WAIT_CANCEL;

    if (millis() - lastPoll >= 90) {
      lastPoll = millis();
      uint8_t r = finger.getImage();
      if (r == FINGERPRINT_PACKETRECIEVEERR || r == FINGERPRINT_BADPACKET)
        return WAIT_COMM;
      if (wantFinger) {
        if (r == FINGERPRINT_OK) {
          setProgress(100);
          return WAIT_OK;
        }
      } else {
        if (r == FINGERPRINT_NOFINGER) {
          setProgress(100);
          return WAIT_OK;
        }
      }
    }

    setProgress((uint8_t)(((millis() - t0) * 100UL) / timeoutMs));
    delay(4);
  }
  return WAIT_TIMEOUT;
}

void enrollFlow(const String &label) {
  drawEnrollScreen(label);

  if (!sensorOnline) {
    failScreen("SENSOR OFFLINE", "CHECK THE WIRING");
    return;
  }

  uint16_t id = allocateId();
  if (id == 0) {
    failScreen("MEMORY FULL", "DELETE A FINGER");
    return;
  }

  Serial.printf("[enroll] start id=%u label=%s\n", id, label.c_str());
  ledSet(LED_ENROLL, FINGERPRINT_LED_BREATHING);

  for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
    WaitResult r = waitFinger(ENROLL_TIMEOUT, "PLACE FINGER", "HOLD STILL", 1, true);
    if (r == WAIT_CANCEL) {
      gotoHome();
      return;
    }
    if (r == WAIT_COMM) {
      sensorLost("enroll/first");
      failScreen("SENSOR LOST", "CHECK THE WIRING");
      return;
    }
    if (r == WAIT_TIMEOUT) {
      failScreen("TIMEOUT", "NO FINGER DETECTED");
      return;
    }

    uint8_t s = captureToSlot(1);
    if (s == FINGERPRINT_PACKETRECIEVEERR || s == FINGERPRINT_BADPACKET) {
      sensorLost("enroll/capture1");
      failScreen("SENSOR LOST", "CHECK THE WIRING");
      return;
    }
    if (s == FINGERPRINT_NOFINGER) {
      if (fpWakeActive())
        retryMsg("NO IMAGE", "PRESS FLAT / CLEAN");
      else
        retryMsg("FINGER REMOVED", "PLACE IT AGAIN");
      continue;
    }
    if (s != FINGERPRINT_OK) {
      retryMsg("IMAGE UNCLEAR", capHint(s));
      continue;
    }

    // refuse to store a finger twice
    uint16_t dupId = 0xFFFF;
    uint16_t dupConf = 0;
    if (searchFinger(dupId, dupConf) == FINGERPRINT_OK && dupId != 0xFFFF &&
        dupConf >= FP_MIN_CONFIDENCE) {
      failScreen("ALREADY SAVED", "ID " + String(dupId) + "  " + nameOf(dupId));
      return;
    }

    r = waitFinger(LIFT_TIMEOUT, "LIFT FINGER", "", 1, false);
    if (r == WAIT_CANCEL) {
      gotoHome();
      return;
    }
    if (r == WAIT_COMM) {
      sensorLost("enroll/lift");
      failScreen("SENSOR LOST", "CHECK THE WIRING");
      return;
    }
    if (r == WAIT_TIMEOUT) {
      retryMsg("FINGER STILL", "LIFT IT UP");
      continue;
    }

    r = waitFinger(ENROLL_TIMEOUT, "PLACE AGAIN", "SAME FINGER", 2, true);
    if (r == WAIT_CANCEL) {
      gotoHome();
      return;
    }
    if (r == WAIT_COMM) {
      sensorLost("enroll/second");
      failScreen("SENSOR LOST", "CHECK THE WIRING");
      return;
    }
    if (r == WAIT_TIMEOUT) {
      failScreen("TIMEOUT", "NO FINGER DETECTED");
      return;
    }

    s = captureToSlot(2);
    if (s == FINGERPRINT_PACKETRECIEVEERR || s == FINGERPRINT_BADPACKET) {
      sensorLost("enroll/capture2");
      failScreen("SENSOR LOST", "CHECK THE WIRING");
      return;
    }
    if (s == FINGERPRINT_NOFINGER) {
      if (fpWakeActive())
        retryMsg("NO IMAGE", "PRESS FLAT / CLEAN");
      else
        retryMsg("FINGER REMOVED", "PLACE IT AGAIN");
      continue;
    }
    if (s != FINGERPRINT_OK) {
      retryMsg("IMAGE UNCLEAR", capHint(s));
      continue;
    }

    s = finger.createModel();
    if (s == FINGERPRINT_ENROLLMISMATCH) {
      retryMsg("MISMATCH", "SAME FINGER, PLEASE");
      continue;
    }
    if (s != FINGERPRINT_OK) {
      failScreen("ENROLL FAILED", errText(s));
      return;
    }

    s = finger.storeModel(id);
    if (s != FINGERPRINT_OK) {
      failScreen("SAVE FAILED", errText(s));
      return;
    }

    saveName(id, label);
    setUsed(id, true);
    saveOcc();
    refreshCount();
    ledSet(LED_OK, FINGERPRINT_LED_FLASHING);
    drawSteps(2);
    setProgress(100);
    setEnrollMsg("SAVED", "ID " + String(id) + "  " + label);
    Serial.printf("[enroll] saved id=%u label=%s\n", id, label.c_str());
    delay(1600);
    ledSet(LED_IDLE, FINGERPRINT_LED_ON);
    gotoHome();
    return;
  }

  failScreen("NOT SAVED", "TOO MANY RETRIES");
}

/* -------------------------------------------------------------------- input */
void handleTouch(const TouchPoint &p) {
  switch (screen) {
    case SCR_HOME:
      if (inside(btnInfo, p)) {
        screen = SCR_INFO;
        probeDone = false;
        probeMs = millis() - 700;   // probe as soon as the screen is up
        drawInfo();
      } else if (inside(btnAdd, p)) {
        screen = SCR_PICK;
        drawPick();
      } else if (inside(btnCheck, p)) {
        startVerify();
      } else if (inside(btnList, p)) {
        listPage = 0;
        screen = SCR_LIST;
        drawList();
      }
      break;

    case SCR_PICK:
      if (inside(btnBack, p) || inside(btnCancel, p)) {
        gotoHome();
        break;
      }
      for (uint8_t i = 0; i < 8; ++i) {
        if (inside(nameBtn(i), p)) {
          enrollFlow(String(FINGER_NAMES[i]));
          return;
        }
      }
      break;

    case SCR_VERIFY:
      if (inside(btnBack, p) || inside(btnBackWide, p)) gotoHome();
      break;

    case SCR_LIST: {
      if (inside(btnBack, p)) {
        gotoHome();
        break;
      }
      collectUsed();
      if (usedN == 0) break;

      if (inside(btnPrev, p)) {
        if (listPage > 0) {
          listPage--;
          drawList();
        }
        break;
      }
      if (inside(btnNext, p)) {
        if ((uint16_t)(listPage + 1) * 5 < usedN) {
          listPage++;
          drawList();
        }
        break;
      }
      if (inside(btnClear, p)) {
        if (askConfirm("CLEAR ALL?", "EVERY FINGER ERASED"))
          clearAll();
        else
          drawList();
        break;
      }

      for (uint8_t i = 0; i < 5; ++i) {
        uint16_t idx = (uint16_t)listPage * 5 + i;
        if (idx >= usedN) break;
        Btn row{6, (int16_t)(42 + i * 38), 158, 34};
        if (inside(row, p)) {
          uint16_t id = usedIds[idx];
          if (askConfirm("DELETE ID " + String(id) + "?", nameOf(id))) {
            if (!deleteId(id)) sensorLost("delete");
          }
          drawList();
          break;
        }
      }
      break;
    }

    case SCR_ENROLL:
      break;

    case SCR_INFO:
      if (inside(btnBack, p) || inside(btnBackWide, p))
        gotoHome();
      else
        drawInfo();
      break;
  }
}

/* --------------------------------------------------------------------- main */
void fpSetup() {
  Serial.println();
  Serial.println("=== SERVICE MODE: R503-S fingerprint manager ===");

  // Дисплей, подсветка и тач уже подняты основной прошивкой — здесь только
  // приводим экран в известное состояние.
  tft.setRotation(0);
  oTouch.setAutoSleep(false);   // иначе первый тап после простоя теряется
  tft.setTextSize(1);        // drawString(font) масштабируется текущим размером
  tft.setTextPadding(0);
  tft.fillScreen(C_BG);
  drawSplash("SENSOR...");

  // R503 WAKEUP (синий провод): idle HIGH, LOW пока палец на сенсоре
  pinMode(PIN_FP_WAKE, FP_WAKE_PULLUP ? INPUT_PULLUP : INPUT);
  wakeRaw = fpWakeActive();
  wakeFinger = wakeRaw;
  wakeRawSince = millis();

  // Датчик. Основная прошивка могла уже открыть Serial2 — повторный begin с
  // теми же пинами безвреден.
  finger.begin(FP_BAUD);
  Serial2.begin(FP_BAUD, SERIAL_8N1, PIN_FP_RX, PIN_FP_TX);
  delay(80);
  for (uint8_t i = 0; i < 5 && !sensorOnline; ++i) {
    sensorOnline = finger.verifyPassword();
    if (!sensorOnline) delay(300);
  }

  loadStore();
  if (sensorOnline) {
    // Жёстко задаём чувствительность: клоны часто оставляют мягкий уровень и
    // потом «узнают» почти любой палец.
    finger.setSecurityLevel(FP_SECURITY_LEVEL);
    uint16_t reportedCap = finger.capacity;
    if (finger.getParameters() == FINGERPRINT_OK) {
      reportedCap = finger.capacity;
      if (finger.capacity > 0 && finger.capacity < MAX_FP_ID) maxId = finger.capacity;
    }
    finger.capacity = maxId;   // диапазон поиска команды 0x04
    refreshCount();
    Serial.printf("[fp] online, %u templates, capacity %u (search %u), level %u\n",
                  storedCount, reportedCap, maxId, finger.security_level);
    reconcile();
    ledSet(LED_IDLE, FINGERPRINT_LED_ON);
  } else {
    Serial.println("[fp] sensor not responding");
  }

  lastSensorRetry = millis();
  screen = SCR_HOME;
  drawHome();
}

void fpLoop() {
  TouchPoint p = pollTouch();
  pollWake();

  if (wakeDirty) {
    wakeDirty = false;
    if (screen == SCR_HOME) renderWakeRow(WAKE_ROW_HOME);
    else if (screen == SCR_VERIFY) renderWakeRow(WAKE_ROW_CHECK);
    else if (screen == SCR_INFO) renderWakeInfo();
  }

  if (screen != SCR_ENROLL) sensorHealth();
  if (screen == SCR_VERIFY) verifyPoll();
  if (screen == SCR_INFO) probeTask();

  if (p.valid) handleTouch(p);

  delay(4);
}

/* ------------------------------------------- вход по PIN в служебный режим */
// Экран PIN-кода служебного режима. PIN берётся из PIN_CODE (credentials.h) и
// передаётся сюда аргументом из основной прошивки, чтобы не тянуть таблицу
// учётных записей в этот модуль.
static const char *SVC_DIGITS[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
static const Btn svcKey[10] = {
    {8, 100, 46, 44}, {62, 100, 46, 44}, {116, 100, 46, 44},
    {8, 148, 46, 44}, {62, 148, 46, 44}, {116, 148, 46, 44},
    {8, 196, 46, 44}, {62, 196, 46, 44}, {116, 196, 46, 44},
    {8, 244, 46, 44}};
static const Btn svcOk{62, 244, 46, 44};
static const Btn svcDel{116, 244, 46, 44};
static const Btn svcField{10, 58, 150, 34};

static String svcPin;
static bool svcError = false;
static const char *svcExpected = "";

void drawServicePin() {
  tft.fillScreen(C_BG);
  tft.setTextSize(1);

  tft.fillRect(0, 0, SCREEN_W, 34, C_PANEL);
  textL("SERVICE", 10, 9, 2, C_ACCENT, C_PANEL);
  textC("ENTER PIN", SCREEN_W / 2, 46, 1, C_MUTED, C_BG);

  tft.fillRoundRect(svcField.x, svcField.y, svcField.w, svcField.h, 8, C_PANEL);
  tft.drawRoundRect(svcField.x, svcField.y, svcField.w, svcField.h, 8,
                    svcError ? C_ERR : C_PANEL2);
  String dots;
  for (uint16_t i = 0; i < svcPin.length() && i < 8; ++i) dots += "*";
  textC(dots, SCREEN_W / 2, svcField.y + svcField.h / 2 + 2, 4, C_TEXT, C_PANEL);

  if (svcError)
    textC("WRONG PIN", SCREEN_W / 2, 302, 1, C_ERR, C_BG);
  else
    textC("REBOOT TO CANCEL", SCREEN_W / 2, 302, 1, C_MUTED, C_BG);

  for (uint8_t i = 0; i < 10; ++i)
    drawButton(svcKey[i], SVC_DIGITS[i], C_PANEL, C_PANEL2, C_TEXT, 2);
  drawButton(svcOk, "OK", C_PANEL, C_ACCENT, C_ACCENT, 2);
  drawButton(svcDel, "<", C_PANEL, C_PANEL2, C_TEXT, 2);
}

bool servicePinCheck() {
  if (svcPin == String(svcExpected)) return true;
  Serial.println("[svc] wrong pin");
  svcPin = "";
  svcError = true;
  drawServicePin();
  delay(700);
  svcError = false;
  drawServicePin();
  return false;
}

bool handleServicePinTouch(const TouchPoint &p) {
  for (uint8_t i = 0; i < 10; ++i) {
    if (inside(svcKey[i], p)) {
      if (svcPin.length() < 16) {
        svcPin += SVC_DIGITS[i];
        svcError = false;
        drawServicePin();
      }
      return false;
    }
  }
  if (inside(svcDel, p)) {
    if (svcPin.length()) svcPin.remove(svcPin.length() - 1);
    svcError = false;
    drawServicePin();
    return false;
  }
  if (inside(svcOk, p)) return servicePinCheck();
  return false;
}

// Вход в служебный режим: PIN, затем бесконечный цикл менеджера отпечатков.
// Возврата нет — выйти можно только перезагрузкой устройства.
void fpManagerRun(const char *pin) {
  svcExpected = pin ? pin : "";
  svcPin = "";
  svcError = false;
  drawServicePin();

  for (;;) {
    TouchPoint p = pollTouch();
    if (p.valid && handleServicePinTouch(p)) break;
    delay(8);
  }

  Serial.println("[svc] pin ok, entering fingerprint manager");
  fpSetup();
  for (;;) fpLoop();
}
