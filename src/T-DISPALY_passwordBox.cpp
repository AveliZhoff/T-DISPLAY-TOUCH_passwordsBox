// ==================== выбор включения мыши ====================
// закомментируйте строку ниже, чтобы отключить активность мыши (противодействие блокировке экрана)
// #define ENABLE_MOUSE
// ====================================================================

// ==================== выбор включения пин-кода ====================
// закомментируйте строку ниже, чтобы отключить запрос пин-кода
#define ENABLE_PIN_CODE
// ====================================================================

// Credential
#include "credentials.h"

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
byte lenHigh = byte((max_y - 0 ) / NUMITEMS(credentials));
byte shift = 5;

// Delays
byte delayBtwnChar = 20;
byte delayBtwnAfterTab = 200;

#ifdef ENABLE_PIN_CODE
// ==== Экран блокировки (ввод пин-кода) ====
bool deviceUnlocked = false;         // устройство разблокировано?
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
  byte x;
  byte y;
  byte w;
  byte h;
};

const PinKey pinKeys[] = {
  {"1",    5,  96, 50, 40},
  {"2",   60,  96, 50, 40},
  {"3",  115,  96, 50, 40},
  {"4",    5, 142, 50, 40},
  {"5",   60, 142, 50, 40},
  {"6",  115, 142, 50, 40},
  {"7",    5, 188, 50, 40},
  {"8",   60, 188, 50, 40},
  {"9",  115, 188, 50, 40},
  {"0",    5, 234, 50, 40},
  {"Enter", 60, 234, 105, 40}
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

  // Если нажата buttonRight и логин не равен "nologin" – отправляем логин
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

// Прототип: основной экран со списком аккаунтов (рисуется после разблокировки)
void drawListScreen();

// Обработка касания на экране пин-кода.
// getLastTouchPosition(y, x) подставляем так же, как в основном цикле:
// первый параметр – вертикаль экрана, второй – горизонталь.
void handlePinTouch(int y, int x) {
  for (byte i = 0; i < NUMITEMS(pinKeys); i++) {
    if ((x >= pinKeys[i].x) && (x <= pinKeys[i].x + pinKeys[i].w) &&
        (y >= pinKeys[i].y) && (y <= pinKeys[i].y + pinKeys[i].h)) {

      // Клавиша Enter – проверяем пин
      if (strcmp(pinKeys[i].label, "Enter") == 0) {
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

// Вывод основного экрана со списком аккаунтов (после разблокировки)
void drawListScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, max_x, max_y, TFT_BLUE);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(3);
  for (int i = 0; i < NUMITEMS(credentials); i++) {
    tft.setCursor(10, shift + lenHigh * i);
    tft.print(credentials[i].name);
  }
}
#endif // ENABLE_PIN_CODE

//////////////////////////////////////////////////////////////
void setup() {
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
  tft.drawRect(0, 0, max_x, max_y, TFT_RED);
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
    tft.drawRect(0, 0, max_x, max_y, TFT_BLUE);
  }

  Keyboard.begin();
#ifdef ENABLE_MOUSE
  Mouse.begin();
#endif

  USB.begin();

#ifdef ENABLE_PIN_CODE
  // Экран ввода пин-кода при каждом запуске/перезагрузке
  drawPinScreen();
#else
  // Вывод списка имён
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(3);
  for (int i = 0; i < NUMITEMS(credentials) ; i++){
    tft.setCursor (10, shift + lenHigh * i); tft.print(credentials[i].name);
  }
#endif // ENABLE_PIN_CODE
  timeStamp = millis();
}

/////////////////////////////////////////////////////////////////
void loop() {
  oTouch.control();

#ifdef ENABLE_PIN_CODE
  // === Экран блокировки (ввод пин-кода) ===
  if (!deviceUnlocked) {
    if (oTouch.hadTouch()) {
      backlight = 1;
      digitalWrite(displayPort, backlight);
      timeStamp = millis();

      int x;
      int y;
      oTouch.getLastTouchPosition(y, x);
      handlePinTouch(y, x);
    }

    // Подсветка гаснет после простоя
    if (millis() > timeStamp + backlightInterval) {
      backlight = 0;
      digitalWrite(displayPort, backlight);
    }
    return;
  }
#endif // ENABLE_PIN_CODE

  buttonRight.loop();
  buttonLeft.loop();
	
  if (oTouch.hadTouch()) {
    backlight = 1;
    digitalWrite(displayPort, backlight);
    timeStamp = millis();
    int x;
    int y;
    oTouch.getLastTouchPosition(y, x);

    // Сброс подсветки предыдущего элемента
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor (10, shift + lenHigh * touchPositionLast);
    tft.print(credentials[touchPositionLast].name);

    for (byte i = 0; i < NUMITEMS(credentials) ; i++){
      if (( y - shift > (i * lenHigh)) && (y - shift < ((i + 1) * lenHigh))) {
        touchPositionLast = i;
        tft.setTextColor(TFT_YELLOW, TFT_BLUE);
        tft.setCursor (10, shift + lenHigh * i);
        tft.print(credentials[i].name);
      }
    }
  }
  
#ifdef ENABLE_MOUSE
  mouse();
#endif
 
  if ( millis() > timeStamp + backlightInterval){
      backlight = 0;
      digitalWrite(displayPort, backlight);
  }
}