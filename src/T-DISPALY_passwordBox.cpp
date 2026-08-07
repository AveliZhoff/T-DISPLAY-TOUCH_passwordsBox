// ==================== выбор включения мыши ====================
// #define ENABLE_MOUSE
// ====================================================================

// Credential
#include "credentials.h"

// Button
#include "Button2.h"
#define BUTTON_PIN_UP 0
#define BUTTON_PIN_DOWN 14
Button2 buttonUp, buttonDown;

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


// Deleays
byte delayBtwnChar = 20;
byte delayBtwnAfterTab = 200;

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

  if ( backlight == 0 ) { return; }
    
  timeStamp = millis();

  // Безопасное сравнение строк
  if (btn == buttonUp && strcmp(credentials[touchPositionLast].login, "nologin") != 0) {

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
 

  str = credentials[touchPositionLast].pass;
  str.toCharArray(charBuf, 50);
  for (int i = 0; i < str.length(); i++){
    Keyboard.print(charBuf[i]);
    delay (delayBtwnChar);
  }
  
  Keyboard.write(KEY_RETURN);
}

/////////////////////////////////////////////////////////////////
void setup() {

  // Button
  buttonUp.begin(BUTTON_PIN_UP);
  buttonDown.begin(BUTTON_PIN_DOWN);
  buttonUp.setClickHandler(click);
  buttonDown.setClickHandler(click);

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

  // Вывод списка имён
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.setTextSize(3);
  for (int i = 0; i < NUMITEMS(credentials) ; i++){
    tft.setCursor (10, shift + lenHigh * i); tft.print(credentials[i].name);
    // tft.drawRect(0, shift + lenHigh * i, max_x, lenHigh, TFT_BLUE); // отладка
  }

  Keyboard.begin();
#ifdef ENABLE_MOUSE
  Mouse.begin();
#endif

  USB.begin();
}

/////////////////////////////////////////////////////////////////
void loop() {

  buttonUp.loop();
  buttonDown.loop();

	oTouch.control();
	
	if (oTouch.hadTouch()) {
    backlight = 1;
    digitalWrite(displayPort, backlight);
    timeStamp = millis();
		int x;
		int y;
		oTouch.getLastTouchPosition(y, x);
    // tft.drawCircle(x, y, 3, TFT_RED);  // отладка

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
/////////////////////////////////////////////////////////////////