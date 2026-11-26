// ---------------------------------------------------------------------------
// TFT_eSPI configuration for LilyGO T-Display-S3 / T-Display-S3 Touch
//
// Same content as the library's User_Setups/Setup206_LilyGo_T_Display_S3.h.
// It is force-included from platformio.ini (-include include/tft_setup.h) so
// that nothing inside .pio/libdeps has to be edited by hand.
//
// NOTE: the display is connected over an 8-bit PARALLEL bus (i8080), NOT SPI.
// ---------------------------------------------------------------------------

#define USER_SETUP_ID 206

#define ST7789_DRIVER
#define INIT_SEQUENCE_3          // better picture quality on this panel

#define CGRAM_OFFSET
#define TFT_RGB_ORDER TFT_RGB

#define TFT_INVERSION_ON

#define TFT_PARALLEL_8_BIT

#define TFT_WIDTH  170
#define TFT_HEIGHT 320

#define TFT_CS   6
#define TFT_DC   7
#define TFT_RST  5

#define TFT_WR   8
#define TFT_RD   9

#define TFT_D0  39
#define TFT_D1  40
#define TFT_D2  41
#define TFT_D3  42
#define TFT_D4  45
#define TFT_D5  46
#define TFT_D6  47
#define TFT_D7  48

#define TFT_BL  38
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT
