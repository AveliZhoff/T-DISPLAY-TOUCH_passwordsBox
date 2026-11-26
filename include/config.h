// ---------------------------------------------------------------------------
// Project configuration: pins, tunables, feature switches.
// Hardware: LilyGO T-Display-S3 Touch + GROW R503-S fingerprint sensor.
// ---------------------------------------------------------------------------
#pragma once

/* ------------------------------------------------------------------ display */
#define SCREEN_W 170
#define SCREEN_H 320

#define PIN_POWER_ON   15   // LCD + peripheral power gate (must be HIGH)
#define PIN_LCD_BL     38   // backlight (driven by TFT_eSPI too)

/* -------------------------------------------------------------------- touch */
#define PIN_I2C_SDA    18   // CST816S SDA (shared I2C bus)
#define PIN_I2C_SCL    17   // CST816S SCL
#define PIN_TOUCH_INT  16   // CST816S INT
#define PIN_TOUCH_RST  21   // CST816S RST

// The touch panel is mounted rotated relative to the LCD, so the library
// returns x = vertical (0..319) and y = horizontal (0..169). main.cpp maps
// that to screen coordinates. If taps land in the wrong place on your unit,
// try enabling one of the options below.
#define TOUCH_SWAP_XY   0
#define TOUCH_INVERT_X  0
#define TOUCH_INVERT_Y  0
#define DEBUG_TOUCH     0    // 1 = print every tap to Serial

/* ------------------------------------------------------------- fingerprint */
// FREE header pins on the T-Display-S3 Touch: GPIO 1, 2, 3, 10, 11, 12, 13, 43, 44.
// GPIO 16/21 belong to the touch controller, GPIO 4 is the battery ADC.
#define PIN_FP_TX       1    // ESP32-S3 TX  -> R503 RX  (brown/green wire)
#define PIN_FP_RX       2    // ESP32-S3 RX  <- R503 TX  (yellow wire)
#define FP_BAUD         57600

// R503 "WAKEUP" output (blue wire): idle HIGH, pulled LOW while a finger
// touches the sensor. Any free GPIO works; GPIO 3 sits next to TX/RX on P2.
#define PIN_FP_WAKE     3
#define FP_WAKE_ACTIVE_LOW 1 // 1 = finger detected when the pin reads LOW
#define FP_WAKE_PULLUP  1    // internal pull-up, harmless for the sensor's output

// Highest template number this firmware will use (the ID bitmap is 128 bits).
#define MAX_FP_ID       127

// Sensor matcher: security level 1 (loosest) .. 5 (strictest). Clones often ship
// with a lax level and then report a match for almost any finger, so it is
// forced here on every boot.
#define FP_SECURITY_LEVEL   3

// A module match is refused when its score is below this value. Raise to 70-90
// if a foreign finger still gets through; lower to 40-45 if the real finger is
// refused too often. The score is shown on the check screen.
#define FP_MIN_CONFIDENCE   55

/* ------------------------------------------------------------------ R503 LED */
// The R503 has a "aura" RGB LED. Colour codes: 1 = red, 2 = blue, 3 = purple,
// 4 = green. Some units only accept red/blue/purple - then set LED_OK to 2.
#define USE_AURA_LED    1
#define LED_IDLE        2    // blue
#define LED_ENROLL      3    // purple
#define LED_OK          4    // green
#define LED_DENY        1    // red

/* -------------------------------------------------------------------- misc */
#define APP_NAME        "R503-S"
#define ENROLL_TIMEOUT  12000   // ms to wait for a finger during enrolment
#define LIFT_TIMEOUT     6000   // ms to wait for the finger to be removed
#define SCAN_INTERVAL     120   // ms between fingerprint polls on the check screen
