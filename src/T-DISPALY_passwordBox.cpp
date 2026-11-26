// ==================== выбор включения мыши ====================
// закомментируйте строку ниже, чтобы отключить активность мыши (противодействие блокировке экрана)
// #define ENABLE_MOUSE
// ====================================================================

// ==================== выбор включения пин-кода ====================
// закомментируйте строку ниже, чтобы отключить запрос пин-кода
// #define ENABLE_PIN_CODE
// ====================================================================

// ==================== выбор включения датчика отпечатков ====================
// раскомментируйте строку ниже для разблокировки датчиком R503-S
#define ENABLE_R503S
// ============================================================================

// ==================== служебный режим управления отпечатками ====================
// Удерживайте правую кнопку при включении: устройство попросит PIN и перейдёт
// в режим управления отпечатками. Выход из режима — только перезагрузка.
#define ENABLE_SERVICE_MODE 1
// ================================================================================

// ==================== взаимоисключающие способы разблокировки ====================
// Активен только один способ. Если включены оба макроса, приоритет у отпечатка:
// PIN-код полностью исключается из сборки.
#if defined(ENABLE_PIN_CODE) && defined(ENABLE_R503S)
  #warning "ENABLE_PIN_CODE и ENABLE_R503S заданы одновременно: работает только ENABLE_R503S"
  #undef ENABLE_PIN_CODE
#endif
// ================================================================================

// Credential
#include "credentials.h"
#include "fp_manager.h"

#ifdef ENABLE_R503S
#include <Adafruit_Fingerprint.h>

// Свободные GPIO этой платы: 1, 2, 3, 10, 11, 12, 13, 43, 44.
// GPIO16 и GPIO21 заняты тач-контроллером CST816 (INT и RST), 5–13 и 38–48 —
// дисплеем, 15 — питанием, 0/14 — кнопками, 4 — ADC батареи.
#define R503_RX_PIN 2
#define R503_TX_PIN 1
// Синий провод датчика (WAKEUP/TOUCH_OUT): LOW, пока палец на сенсоре.
// Нужен, чтобы включать подсветку в момент касания, а не после ответа по UART.
#define R503_WAKE_PIN 3
const uint32_t r503BaudRate = 57600;
const unsigned long r503ScanInterval = 150;
const unsigned long r503RetryInterval = 5000;

// Достоверность совпадения: у клонов с мягким уровнем «узнаётся» любой палец.
#define R503_SECURITY_LEVEL 3
#define R503_SEARCH_RANGE 127
#define R503_MIN_CONFIDENCE 55

// Светодиод R503 (Aura LED): 1 красный, 2 синий, 3 фиолетовый, 4 зелёный.
#define R503_LED_IDLE 2
#define R503_LED_SCAN 3
#define R503_LED_OK 4
#define R503_LED_FAIL 1

Adafruit_Fingerprint r503(&Serial2);
bool r503Ready = false;
unsigned long r503LastScan = 0;
unsigned long r503LastRetry = 0;
uint8_t r503Misses = 0;

// Флаг из прерывания по синему проводу: палец коснулся сенсора.
volatile bool r503WakeFlag = false;
void IRAM_ATTR r503WakeISR() { r503WakeFlag = true; }

// Состояние светодиода: 0 выкл, 1 дежурный, 2 сканирование, 3 успех, 4 отказ.
enum : uint8_t { LED_OFF, LED_IDLE, LED_SCAN, LED_OK, LED_FAIL };
uint8_t r503LedState = 255;      // 255 — команда ещё не отправлялась
bool r503LedOk = true;
uint8_t r503LedFails = 0;
unsigned long r503LedUntil = 0;
unsigned long r503LedBeat = 0;

// Состояние экрана блокировки.
enum : uint8_t { LS_IDLE, LS_SCAN, LS_OK, LS_FAIL, LS_OFFLINE };
uint8_t lockState = LS_IDLE;
uint16_t lockId = 0;
uint16_t lockConf = 0;
uint8_t lockShownState = 255;
uint16_t lockShownId = 0xFFFF;
uint16_t lockShownConf = 0xFFFF;
#endif // ENABLE_R503S

void drawListScreen();   // нужен и в сборке без блокировки

#if defined(ENABLE_PIN_CODE) || defined(ENABLE_R503S)
bool deviceUnlocked = false;
#endif

// Button
#include "Button2.h"
#define BUTTON_PIN_RIGHT 14
#define BUTTON_PIN_LEFT 0
Button2 buttonRight, buttonLeft;

// Touch
#include <Wire.h>
#include <CST816_TouchLib.h>
#define PIN_TFT_POWER_ON	15
#define I2C_SDA				18
#define I2C_SCL				17
using namespace MDO;
DummyTouchSubscriber oTouchSubriber;
CST816Touch oTouch;
byte touchPositionLast = 0;

// USB
#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"
USBHIDKeyboard Keyboard;
char charBuf[50];
String str;

#ifdef ENABLE_MOUSE
  #include "USBHIDMouse.h"
  USBHIDMouse Mouse;
  unsigned long mouseTimestamp;
  const unsigned long mouseTimeInterval = 60000;
  byte moveNum = 1;
  int moveTicks = 3;
#endif // ENABLE_MOUSE

// Display
#include <SPI.h>
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
int max_x = 170;
int max_y = 320;
unsigned long timeStamp;
unsigned long const backlightInterval = 30000;
bool backlight = 1;
byte displayPort = 38;
// ---- палитра интерфейса (как в проекте T-DISPLAY-TOUCH_fingerprints_R503-S)
static constexpr uint16_t uiRgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t LC_BG     = uiRgb(9, 12, 20);
const uint16_t LC_PANEL  = uiRgb(24, 30, 44);
const uint16_t LC_PANEL2 = uiRgb(44, 54, 76);
const uint16_t LC_ACCENT = uiRgb(0, 196, 255);
const uint16_t LC_TEXT   = uiRgb(234, 240, 250);
const uint16_t LC_MUTED  = uiRgb(120, 134, 158);
const uint16_t LC_OK     = uiRgb(40, 220, 130);
const uint16_t LC_ERR    = uiRgb(255, 76, 76);

#if defined(ENABLE_PIN_CODE) || defined(ENABLE_R503S)
#ifdef ENABLE_PIN_CODE
void drawPinScreen();
#endif
#ifdef ENABLE_R503S
void drawLockScreen();
#endif

// Пробудить экран и продлить таймер простоя (касание экрана или датчика).
// Кадр к этому моменту уже нарисован (он рисуется при блокировке), поэтому
// достаточно включить подсветку — без перерисовки, чтобы не было мигания.
void displayWake() {
  if (!backlight) {
    backlight = 1;
    digitalWrite(displayPort, HIGH);
  }
  timeStamp = millis();
}
#endif

// Delays
byte delayBtwnChar = 20;
byte delayBtwnAfterTab = 200;

#ifdef ENABLE_PIN_CODE
// ==== Экран блокировки (ввод пин-кода) ====
String pinInput = "";                // введённый пин

// Поле ввода пин-кода
#define PIN_FIELD_X 5
#define PIN_FIELD_Y 5
#define PIN_FIELD_W 160
#define PIN_FIELD_H 45

// Максимум цифр в поле ввода – сколько помещается визуально
// (шрифт textSize 3: 18 px на цифру, отступы по 7 px с краёв)
const byte pinMaxLength = (PIN_FIELD_W - 14) / 18;

// Клавиши цифровой клавиатуры
struct PinKey {
  const char* label;
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
};

const PinKey pinKeys[] = {
  {"1",    5,  71, 50, 40},
  {"2",   60,  71, 50, 40},
  {"3",  115,  71, 50, 40},
  {"4",    5, 117, 50, 40},
  {"5",   60, 117, 50, 40},
  {"6",  115, 117, 50, 40},
  {"7",    5, 163, 50, 40},
  {"8",   60, 163, 50, 40},
  {"9",  115, 163, 50, 40},
  {"0",    5, 209, 50, 40},
  {"Enter", 60, 209, 105, 40},
  {"Clear",  5, 270, 160, 45}
};
#endif // ENABLE_PIN_CODE


/////////////////////////////////////////////////////////////////
#ifdef ENABLE_MOUSE
  void mouse () {
    if (millis() > mouseTimestamp + mouseTimeInterval ) {
      mouseTimestamp=millis();
      if ( moveNum == 1 ) {
        Serial.println("Move mouse pointer up");
        Mouse.move(0,-moveTicks);
      }
      if ( moveNum == 2 ) {
        Serial.println("Move mouse pointer left");
        Mouse.move(-moveTicks,0);
      }
      if ( moveNum == 3 ) {
        Serial.println("Move mouse pointer down");
        Mouse.move(0,moveTicks);
      }
      if ( moveNum ==4 ) {
        Serial.println("Move mouse pointer right");
        Mouse.move(moveTicks,0);
      }
      moveNum++; if (moveNum > 4) {moveNum = 1;};
    }
  }
#endif // ENABLE_MOUSE

/////////////////////////////////////////////////////////////////
void click(Button2& btn) {
  if ( backlight == 0 ) return;
  timeStamp = millis();

  // Логин и TAB отправляем только по короткому нажатию левой кнопки
  // и только если логин не равен "nologin"
  if (btn == buttonLeft && strcmp(credentials[touchPositionLast].login, "nologin") != 0) {
    str = credentials[touchPositionLast].login;
    str.toCharArray(charBuf, 50);
    for (int i = 0; i < str.length(); i++){
      Serial.print("Sending Login: "); Serial.println(charBuf[i]);
      Keyboard.print(charBuf[i]);
      delay (delayBtwnChar);
    }
    Keyboard.write(KEY_TAB);
    delay (delayBtwnAfterTab);
  }

  // Всегда отправляем пароль и Enter (для короткого нажатия)
  str = credentials[touchPositionLast].pass;
  str.toCharArray(charBuf, 50);
  for (int i = 0; i < str.length(); i++){
    Keyboard.print(charBuf[i]);
    delay (delayBtwnChar);
  }
  Keyboard.write(KEY_RETURN);
}

// Обработчик долгого нажатия:
//   правая кнопка – вводит только пароль,
//   левая кнопка  – вводит только логин
void longClick(Button2& btn) {
  if (backlight == 0) return;
  timeStamp = millis();

  // Правая кнопка – только пароль
  if (btn == buttonRight) {
    str = credentials[touchPositionLast].pass;
    str.toCharArray(charBuf, 50);
    for (int i = 0; i < str.length(); i++){
      Serial.print("Sending Password: "); Serial.println(charBuf[i]);
      Keyboard.print(charBuf[i]);
      delay(delayBtwnChar);
    }
    return;
  }

  // Левая кнопка – только логин (если он задан)
  if (strcmp(credentials[touchPositionLast].login, "nologin") != 0) {
    str = credentials[touchPositionLast].login;
    str.toCharArray(charBuf, 50);
    for (int i = 0; i < str.length(); i++){
      Serial.print("Sending Login: "); Serial.println(charBuf[i]);
      Keyboard.print(charBuf[i]);
      delay(delayBtwnChar);
    }
  }
}

#ifdef ENABLE_PIN_CODE
//// ==== Функции экрана блокировки (ввод пин-кода) ====

// Перерисовка поля ввода пин-кода
void drawPinField() {
  tft.fillRect(PIN_FIELD_X + 1, PIN_FIELD_Y + 1, PIN_FIELD_W - 2, PIN_FIELD_H - 2, TFT_NAVY);
  tft.drawRect(PIN_FIELD_X, PIN_FIELD_Y, PIN_FIELD_W, PIN_FIELD_H, TFT_SKYBLUE);

  // Введённые цифры
  tft.setTextColor(TFT_WHITE, TFT_NAVY); tft.setTextSize(3);
  tft.setCursor(PIN_FIELD_X + 7, PIN_FIELD_Y + 10);
  tft.print(pinInput);

  // Курсор (подчёркивание) перед следующей цифрой
  if (pinInput.length() < pinMaxLength) {
    int cursorX = PIN_FIELD_X + 7 + pinInput.length() * 18;
    tft.fillRect(cursorX, PIN_FIELD_Y + 32, 16, 5, TFT_WHITE);
  }
}

// Отрисовка одной клавиши клавиатуры
void drawPinKey(byte index, bool pressed) {
  uint16_t fill   = pressed ? TFT_WHITE    : TFT_DARKGREY;
  uint16_t border = pressed ? TFT_SKYBLUE  : TFT_WHITE;
  uint16_t text   = pressed ? TFT_BLACK    : TFT_WHITE;

  tft.fillRoundRect(pinKeys[index].x, pinKeys[index].y, pinKeys[index].w, pinKeys[index].h, 6, fill);
  tft.drawRoundRect(pinKeys[index].x, pinKeys[index].y, pinKeys[index].w, pinKeys[index].h, 6, border);

  if (strcmp(pinKeys[index].label, "Enter") == 0) {
    tft.setTextColor(text, fill); tft.setTextSize(2);
    tft.setCursor(pinKeys[index].x + 22, pinKeys[index].y + 12);
    tft.print("Enter");
  } else if (strcmp(pinKeys[index].label, "Clear") == 0) {
    tft.setTextColor(text, fill); tft.setTextSize(2);
    tft.setCursor(pinKeys[index].x + 50, pinKeys[index].y + 14);
    tft.print("Clear");
  } else {
    tft.setTextColor(text, fill); tft.setTextSize(3);
    tft.setCursor(pinKeys[index].x + 16, pinKeys[index].y + 8);
    tft.print(pinKeys[index].label);
  }
}

// Полная отрисовка экрана ввода пин-кода
void drawPinScreen() {
  tft.fillScreen(TFT_BLACK);
  drawPinField();
  for (byte i = 0; i < NUMITEMS(pinKeys); i++) {
    drawPinKey(i, false);
  }
}

// Обработка касания на экране пин-кода.
// getLastTouchPosition(y, x) подставляем так же, как в основном цикле:
// первый параметр – вертикаль экрана, второй – горизонталь.
void handlePinTouch(int y, int x) {
  for (byte i = 0; i < NUMITEMS(pinKeys); i++) {
    if ((x >= pinKeys[i].x) && (x <= pinKeys[i].x + pinKeys[i].w) &&
        (y >= pinKeys[i].y) && (y <= pinKeys[i].y + pinKeys[i].h)) {

      // Кнопка Clear очищает введённый пин без проверки
      if (strcmp(pinKeys[i].label, "Clear") == 0) {
        pinInput = "";
        drawPinField();
      // Клавиша Enter – проверяем пин
      } else if (strcmp(pinKeys[i].label, "Enter") == 0) {
        if (pinInput == PIN_CODE) {
          deviceUnlocked = true;
          drawListScreen();
        } else {
          // Неверный пин – очищаем поле ввода и начинаем заново
          pinInput = "";
          drawPinField();
        }
      } else {
        // Цифровая клавиша – добавляем цифру в поле ввода
        if (pinInput.length() < pinMaxLength) {
          pinInput += pinKeys[i].label;
          drawPinField();
        }
      }
      break;
    }
  }
}

#endif // ENABLE_PIN_CODE

// ---- экран списка учётных записей (после разблокировки) --------------------
// Строки делят высоту экрана поровну и без зазоров: зона нажатия каждой строки
// равна её высоте, поэтому «мёртвых» полос между ними нет, а сама строка
// получается максимально крупной для пальца.
#define LIST_TOP    2
#define LIST_BOTTOM 2

int16_t listRowTop(uint8_t i) {
  const int span = max_y - LIST_TOP - LIST_BOTTOM;
  return (int16_t)(LIST_TOP + (int)i * span / (int)NUMITEMS(credentials));
}

// Строка под координатой y (вся площадь экрана занята строками).
uint8_t listRowAt(int16_t y) {
  for (uint8_t i = 0; i < NUMITEMS(credentials); ++i)
    if (y < listRowTop((uint8_t)(i + 1))) return i;
  return (uint8_t)(NUMITEMS(credentials) - 1);
}

void drawListRow(uint8_t i, bool selected) {
  const int16_t y0 = listRowTop(i);
  const int16_t h  = listRowTop((uint8_t)(i + 1)) - y0;
  const int16_t x  = 6;
  const int16_t w  = max_x - 12;

  uint16_t fill = selected ? LC_ACCENT : LC_PANEL;
  tft.fillRoundRect(x, y0 + 1, w, h - 2, 6, fill);
  if (!selected) tft.drawRoundRect(x, y0 + 1, w, h - 2, 6, LC_PANEL2);

  // Крупный шрифт (16x26). Если имя длинное и не влезает в карточку —
  // откатываемся на мелкий, чтобы текст не обрезался.
  uint8_t font = 4;
  if (tft.textWidth(credentials[i].name, font) > (uint16_t)(w - 20)) font = 2;

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(selected ? LC_BG : LC_TEXT, fill);
  tft.drawString(credentials[i].name, x + 10, y0 + h / 2, font);
  tft.setTextDatum(TL_DATUM);
}

void drawListScreen() {
  tft.setTextSize(1);   // drawString(font) масштабируется текущим размером
  tft.fillScreen(LC_BG);
  // Рамка — признак разблокированного экрана.
  tft.drawRect(0, 0, max_x, max_y, LC_ACCENT);
  for (uint8_t i = 0; i < NUMITEMS(credentials); ++i)
    drawListRow(i, i == touchPositionLast);
}

#ifdef ENABLE_R503S
// ---------------------------------------------------------------- светодиод
// Синий — устройство заблокировано; при касании датчика мигает фиолетовым;
// после проверки коротко зелёный/красный; после разблокировки не горит.
void r503LedSend(uint8_t func, uint8_t color) {
  uint8_t r = r503.LEDcontrol(func, color, color, 0);
  Serial.printf("[r503] led func=%u color=%u -> 0x%02X\n", func, color, r);
  if (r == FINGERPRINT_OK) {
    r503LedFails = 0;
  } else if (++r503LedFails >= 3) {
    r503LedOk = false;
    Serial.println("[r503] aura led unsupported, indication disabled");
  }
}

void r503LedApply(uint8_t state) {
  if (!r503Ready || !r503LedOk) return;
  if (state == r503LedState) return;
  r503LedState = state;

  switch (state) {
    case LED_OFF:
      // Цвет 1, а не 0: часть модулей отвергает недопустимый цвет в параметре.
      r503LedSend(FINGERPRINT_LED_OFF, R503_LED_FAIL);
      r503.LEDcontrol(false);   // дублируем командой 0x51 «погасить индикатор»
      break;
    case LED_SCAN:
      r503LedSend(FINGERPRINT_LED_FLASHING, R503_LED_SCAN);
      break;
    case LED_OK:
      r503LedSend(FINGERPRINT_LED_ON, R503_LED_OK);
      break;
    case LED_FAIL:
      r503LedSend(FINGERPRINT_LED_ON, R503_LED_FAIL);
      break;
    default:
      r503LedSend(FINGERPRINT_LED_ON, R503_LED_IDLE);
      break;
  }
}

void r503LedRest() { r503LedApply(deviceUnlocked ? LED_OFF : LED_IDLE); }

void r503LedShow(uint8_t state, unsigned long ms) {
  r503LedApply(state);
  r503LedUntil = millis() + ms;
}

void r503LedService() {
  // Снять временную индикацию (зелёный/красный) по таймеру.
  if (r503LedUntil && (long)(millis() - r503LedUntil) >= 0) {
    r503LedUntil = 0;
    r503LedRest();
  }

  // Раз в 5 секунд подтверждать дежурное состояние: если команда потерялась
  // или модуль её проигнорировал, индикация восстановится сама.
  uint8_t want = deviceUnlocked ? LED_OFF : LED_IDLE;
  if (!r503LedUntil && r503LedState == want && millis() - r503LedBeat > 5000) {
    r503LedBeat = millis();
    r503LedState = 255;
    r503LedRest();
  }
}

// ---------------------------------------------------------- экран блокировки
void renderLockFrame() {
  if (lockState == lockShownState && lockId == lockShownId && lockConf == lockShownConf)
    return;
  lockShownState = lockState;
  lockShownId = lockId;
  lockShownConf = lockConf;

  tft.fillRect(0, 50, max_x, 200, LC_BG);
  tft.setTextSize(1);   // drawString(font) масштабируется текущим textsize

  uint16_t ring = LC_ACCENT;
  uint16_t vcol = LC_MUTED;
  const char *verdict = "READY";
  String l1 = "PLACE FINGER";
  String l2 = "ON THE SENSOR";

  switch (lockState) {
    case LS_SCAN:
      verdict = "SCAN";
      vcol = LC_ACCENT;
      l1 = "HOLD STILL";
      l2 = "";
      break;
    case LS_OK:
      ring = LC_OK;
      verdict = "MATCH";
      vcol = LC_OK;
      l1 = "ID " + String(lockId);
      l2 = "CONFIDENCE " + String(lockConf);
      break;
    case LS_FAIL:
      ring = LC_ERR;
      verdict = "DENIED";
      vcol = LC_ERR;
      l1 = lockId ? ("ID " + String(lockId)) : String("UNKNOWN FINGER");
      l2 = lockConf ? ("SCORE " + String(lockConf) + " MIN " + String(R503_MIN_CONFIDENCE))
                    : String("");
      break;
    case LS_OFFLINE:
      ring = LC_ERR;
      verdict = "OFFLINE";
      vcol = LC_ERR;
      l1 = "SENSOR NOT FOUND";
      l2 = "CHECK THE WIRING";
      break;
    default:
      break;
  }

  tft.drawCircle(85, 120, 30, ring);
  tft.drawCircle(85, 120, 23, ring);
  tft.drawCircle(85, 120, 16, ring);
  tft.fillCircle(85, 120, 6, ring);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(vcol, LC_BG);
  tft.drawString(verdict, 85, 182, 4);
  tft.setTextColor(LC_TEXT, LC_BG);
  tft.drawString(l1, 85, 212, 2);
  tft.setTextColor(LC_MUTED, LC_BG);
  tft.drawString(l2, 85, 234, 2);
  tft.setTextDatum(TL_DATUM);
}

void drawLockScreen() {
  tft.fillScreen(LC_BG);
  tft.setTextSize(1);   // после списка аккаунтов остаётся 3 — иначе шрифты втрое
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(LC_ACCENT, LC_BG);
  tft.drawString("FINGERPRINT", 10, 9, 2);

  tft.setTextColor(r503Ready ? LC_OK : LC_ERR, LC_BG);
  tft.drawString(r503Ready ? "SENSOR OK" : "NO SENSOR", 10, 40, 1);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(LC_MUTED, LC_BG);
  tft.drawString("LOCKED", 160, 40, 1);

  tft.setTextDatum(MC_DATUM);
  tft.drawString("TOUCH TO WAKE", 85, 292, 1);
  tft.setTextDatum(TL_DATUM);

  lockShownState = 255;   // перерисовать кадр целиком
  renderLockFrame();
}

// Вернуть устройство в состояние «как при включении» (вызывается при гашении
// подсветки по таймауту).
void lockDevice() {
  deviceUnlocked = false;
  lockState = LS_IDLE;
  lockId = 0;
  lockConf = 0;
  r503LedUntil = 0;
  r503LedState = 255;
  r503WakeFlag = false;
  drawLockScreen();
  r503LedRest();
}

void initializeR503() {
  // begin() сам вызывает Serial2.begin(baud) с пинами по умолчанию, поэтому
  // сначала он, а уже потом привязка к нашим свободным GPIO.
  r503.begin(r503BaudRate);
  Serial2.begin(r503BaudRate, SERIAL_8N1, R503_RX_PIN, R503_TX_PIN);
  delay(100);

  r503Ready = r503.verifyPassword();
  if (r503Ready) {
    r503.setSecurityLevel(R503_SECURITY_LEVEL);
    r503.getParameters();
    r503.capacity = R503_SEARCH_RANGE;   // диапазон поиска 0x04, не зависит от SysPara
  }
  r503LastRetry = millis();
  Serial.printf("[r503] %s (level %u, search range %u)\n",
                r503Ready ? "online" : "not responding", R503_SECURITY_LEVEL,
                R503_SEARCH_RANGE);

  // Синий провод: касание сенсора даёт фронт вниз — будим подсветку сразу,
  // не дожидаясь ответа по UART (~200+ мс).
  pinMode(R503_WAKE_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(R503_WAKE_PIN), r503WakeISR, FALLING);

  r503LedState = 255;   // отправить первую команду светодиода
  r503LedRest();        // заблокировано → синий
}

bool isR503CommunicationError(uint8_t result) {
  return result == FINGERPRINT_PACKETRECIEVEERR ||
         result == FINGERPRINT_BADPACKET;
}

// millis()-безопасная проверка «время пришло» (без переполнения на 49-х сутках).
bool r503Due(unsigned long next) {
  return (long)(millis() - next) >= 0;
}

// Снимок делается не в момент касания (первый кадр смазан), а после паузы.
// Ответ 0x02 «нет пальца» при прижатом пальце бывает временным — терпим
// несколько подряд, только потом считаем, что палец убран.
//
// Все паузы внутри идут через r503Pump(): пока идёт съёмка, тач продолжает
// опрашиваться, иначе касание в это окно теряется и подсветка не включается
// с первого раза.
void r503Pump(uint16_t ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    // Палец на сенсоре — сразу будим экран (прерывание по синему проводу).
    if (r503WakeFlag) {
      r503WakeFlag = false;
      displayWake();
    }
    oTouch.control();
    if (oTouch.hadTouch()) {
      int ty, tx;
      oTouch.getLastTouchPosition(ty, tx);
      displayWake();
    }
    delay(4);
  }
}

uint8_t r503CaptureToSlot(uint8_t slot) {
  uint8_t lastErr = FINGERPRINT_IMAGEFAIL;

  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    r503Pump(200);
    while (Serial2.available()) Serial2.read();   // сброс запоздавших байт

    uint8_t r = r503.getImage();
    if (isR503CommunicationError(r)) return r;
    if (r == FINGERPRINT_NOFINGER) {
      if (++r503Misses >= 3) return FINGERPRINT_NOFINGER;
      r503Pump(80);
      continue;
    }
    r503Misses = 0;

    if (r != FINGERPRINT_OK) {
      lastErr = r;
      r503Pump(80);
      continue;
    }

    // Палец на датчике: будим подсветку и показываем сканирование.
    displayWake();
    if (lockState != LS_SCAN) {
      lockState = LS_SCAN;
      renderLockFrame();
    }
    r503LedApply(LED_SCAN);

    uint8_t s = r503.image2Tz(slot);
    if (s == FINGERPRINT_OK) return FINGERPRINT_OK;
    lastErr = s;
    if (isR503CommunicationError(s)) return s;
    r503Pump(80);
  }
  return lastErr;
}

// Поиск 1:N. Классическая команда 0x04 идёт первой: часть экземпляров не
// поддерживает быстрый поиск 0x1B и отвечает на него «OK» с пустым payload,
// из-за чего библиотека возвращает «совпадение ID 0 / score 0» на любой палец.
// Поэтому 0x1B — только фолбэк, а совпадением считается ответ с валидным ID
// (не 0xFFFF) и ненулевым счётом.
uint8_t r503SearchFinger(uint16_t &id, uint16_t &conf) {
  uint8_t s = r503.fingerSearch(1);
  id = r503.fingerID;
  conf = r503.confidence;
  Serial.printf("[r503] search 0x04 -> 0x%02X id=%u conf=%u\n", s, id, conf);

  bool usable = (s == FINGERPRINT_OK && id != 0xFFFF && conf > 0) ||
                (s == FINGERPRINT_NOTFOUND);
  if (!usable) {
    uint8_t f = r503.fingerFastSearch();
    Serial.printf("[r503] search 0x1B -> 0x%02X id=%u conf=%u\n", f, r503.fingerID,
                  r503.confidence);
    if (f == FINGERPRINT_OK && r503.fingerID != 0xFFFF && r503.confidence > 0) {
      s = f;
      id = r503.fingerID;
      conf = r503.confidence;
    } else if (f == FINGERPRINT_NOTFOUND) {
      s = FINGERPRINT_NOTFOUND;
      id = 0xFFFF;
      conf = 0;
    }
  }
  return s;
}

void r503LoseLink(const char *where) {
  Serial.printf("[r503] link lost (%s)\n", where);
  r503Ready = false;
  r503LastRetry = millis();
  r503LedState = 255;
  lockState = LS_OFFLINE;
  drawLockScreen();
}

void checkR503Fingerprint() {
  unsigned long now = millis();

  if (!r503Ready) {
    if (now - r503LastRetry < r503RetryInterval) return;
    r503LastRetry = now;
    r503Ready = r503.verifyPassword();
    if (r503Ready) {
      r503.setSecurityLevel(R503_SECURITY_LEVEL);
      r503.getParameters();
      r503.capacity = R503_SEARCH_RANGE;
      r503LedState = 255;
      r503LedRest();
      lockState = LS_IDLE;
      Serial.println("[r503] reconnected");
    } else {
      lockState = LS_OFFLINE;
    }
    drawLockScreen();
    if (!r503Ready) return;
  }

  if (!r503Due(r503LastScan)) return;
  r503LastScan = millis() + r503ScanInterval;

  uint8_t cap = r503CaptureToSlot(1);
  if (isR503CommunicationError(cap)) {
    r503LoseLink("capture");
    return;
  }
  if (cap == FINGERPRINT_NOFINGER) {
    if (lockState == LS_SCAN) {
      lockState = LS_IDLE;
      renderLockFrame();
    }
    r503LedRest();
    return;
  }
  if (cap != FINGERPRINT_OK) {
    lockState = LS_FAIL;
    lockId = 0;
    lockConf = 0;
    renderLockFrame();
    r503LedShow(LED_FAIL, 1000);
    r503LastScan = millis() + 500;   // не частить с повторной съёмкой
    return;
  }

  uint16_t id = 0xFFFF;
  uint16_t conf = 0;
  uint8_t s = r503SearchFinger(id, conf);
  if (isR503CommunicationError(s)) {
    r503LoseLink("search");
    return;
  }

  // Разблокировка только по достоверному ответу: валидный ID и счёт не ниже порога.
  if (s == FINGERPRINT_OK && id != 0xFFFF && conf >= R503_MIN_CONFIDENCE) {
    lockState = LS_OK;
    lockId = id;
    lockConf = conf;
    renderLockFrame();
    r503LedShow(LED_OK, 1500);
    Serial.printf("[r503] UNLOCK id=%u conf=%u\n", id, conf);
    delay(400);                 // короткое подтверждение на экране и зелёный LED

    deviceUnlocked = true;
    drawListScreen();
    return;
  }

  lockState = LS_FAIL;
  lockId = (s == FINGERPRINT_OK) ? id : 0;
  lockConf = (s == FINGERPRINT_OK) ? conf : 0;
  renderLockFrame();
  r503LedShow(LED_FAIL, 1200);

  if (s == FINGERPRINT_OK)
    Serial.printf("[r503] weak match id=%u conf=%u (min %u), locked\n", id, conf,
                  R503_MIN_CONFIDENCE);
  else
    Serial.printf("[r503] no match (status 0x%02X)\n", s);

  r503LastScan = millis() + 1500;    // пауза, чтобы красный был виден и не долбить сенсор
}
#endif // ENABLE_R503S

//////////////////////////////////////////////////////////////
void setup() {
  // Правая кнопка, удержанная при включении, открывает служебный режим
  // управления отпечатками (после ввода PIN). Кнопки активны низким уровнем.
  pinMode(BUTTON_PIN_RIGHT, INPUT_PULLUP);
  delay(20);
  bool serviceRequested = (digitalRead(BUTTON_PIN_RIGHT) == LOW);

  // Button
  buttonRight.begin(BUTTON_PIN_RIGHT);
  buttonLeft.begin(BUTTON_PIN_LEFT);
  buttonRight.setClickHandler(click);
  buttonLeft.setClickHandler(click);
  buttonRight.setLongClickHandler(longClick);
  buttonLeft.setLongClickHandler(longClick);

  // Display
  tft.init();
  tft.setRotation(0);
  // tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);
  // Рамку по краю здесь не рисуем: она остаётся висеть до появления экрана
  // блокировки (инициализация датчика занимает до секунды). Синяя рамка
  // разблокированного экрана рисуется в drawListScreen().
  backlight = 1;
  digitalWrite(displayPort, backlight);

  // Touch
  pinMode(PIN_TFT_POWER_ON, OUTPUT);
  digitalWrite(PIN_TFT_POWER_ON, HIGH);
	
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
	
  if (!oTouch.begin(Wire, &oTouchSubriber)) {
    tft.setTextColor(TFT_RED, TFT_BLACK); tft.setTextSize(3); tft.setCursor (10, 10); tft.print("Touch Err");
    while(true){
      delay(100);
    }
  }

  CST816Touch::device_type_t eDeviceType;
  if (oTouch.getDeviceType(eDeviceType)) {
    Serial.println("[touch] device detected");
  }

  Keyboard.begin();
#ifdef ENABLE_MOUSE
  Mouse.begin();
#endif

  USB.begin();

#if ENABLE_SERVICE_MODE
  // Кнопку всё ещё держат — спрашиваем PIN и уходим в менеджер отпечатков.
  // fpManagerRun() не возвращает управление: выход — только перезагрузка.
  if (serviceRequested || digitalRead(BUTTON_PIN_RIGHT) == LOW) {
    fpManagerRun(PIN_CODE);
  }
#else
  (void)serviceRequested;
#endif

#ifdef ENABLE_R503S
  initializeR503();
#endif

#ifdef ENABLE_PIN_CODE
  drawPinScreen();
#elif defined(ENABLE_R503S)
  drawLockScreen();
#else
  // Сборка без блокировки: сразу экран списка аккаунтов.
  drawListScreen();
#endif
  timeStamp = millis();
}

/////////////////////////////////////////////////////////////////
void loop() {
  oTouch.control();

#ifdef ENABLE_R503S
  r503LedService();     // гасит временную индикацию (зелёный/красный) по таймеру
#endif

#if defined(ENABLE_PIN_CODE) || defined(ENABLE_R503S)
  if (!deviceUnlocked) {
#ifdef ENABLE_R503S
    // Касание сенсора (синий провод, GPIO 3) включает подсветку сразу —
    // уровень/флаг проверяются до обращения к датчику по UART.
    if (r503WakeFlag || (!backlight && digitalRead(R503_WAKE_PIN) == LOW)) {
      r503WakeFlag = false;
      displayWake();
    }

    checkR503Fingerprint();
    if (deviceUnlocked) return;
#endif

    if (oTouch.hadTouch()) {
      bool wasBacklightOn = backlight;
      int x;
      int y;
      oTouch.getLastTouchPosition(y, x);

      displayWake();     // подсветка + продление таймера простоя

      if (wasBacklightOn) {
#ifdef ENABLE_PIN_CODE
        handlePinTouch(y, x);
#endif
      }
    }

    // Подсветка гаснет после простоя
    if (millis() > timeStamp + backlightInterval) {
      backlight = 0;
      digitalWrite(displayPort, backlight);
    }
    return;
  }
#endif

  buttonRight.loop();
  buttonLeft.loop();
	
  if (oTouch.hadTouch()) {
    backlight = 1;
    digitalWrite(displayPort, backlight);
    timeStamp = millis();
    int x;
    int y;
    oTouch.getLastTouchPosition(y, x);

    // Перерисовываем только две строки: снятую и выбранную.
    uint8_t idx = listRowAt((int16_t)y);
    if (idx != touchPositionLast) {
      uint8_t prev = touchPositionLast;
      touchPositionLast = idx;
      drawListRow(prev, false);
      drawListRow(idx, true);
    }
  }
  
#ifdef ENABLE_MOUSE
  mouse();
#endif
 
  if ( millis() > timeStamp + backlightInterval){
      backlight = 0;
      digitalWrite(displayPort, backlight);
#ifdef ENABLE_R503S
      // Вместе с подсветкой — снова блокируем устройство, как при включении.
      lockDevice();
#endif
  }
}