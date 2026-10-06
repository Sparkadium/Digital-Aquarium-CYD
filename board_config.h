#pragma once
// Original ESP32-2432S028R CYD: ILI9341 TFT + XPT2046 resistive touch.
#define BOARD_NAME "CYD ESP32-2432S028R"
#define GLOWTANK_RELEASE "CYD 1.3 render optimization"
#define LCD_DC 2
#define LCD_CS 15
#define LCD_SCK 14
#define LCD_MOSI 13
#define LCD_MISO 12
#define LCD_RST GFX_NOT_DEFINED
#define LCD_BL 21
#define LCD_ROTATION 0
#define LCD_SWAP_RB 0
#define LCD_W 240
#define LCD_H 320
#define PIN_BOOT 0
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_SCK 25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#ifndef CYD_TOUCH_FEED
#define CYD_TOUCH_FEED 1
#endif
#ifndef BACKLIGHT_LEVEL
#define BACKLIGHT_LEVEL 255  // full-on GPIO avoids PWM as a possible blank-screen cause
#endif
#ifndef LCD_STARTUP_CHECK_MS
#define LCD_STARTUP_CHECK_MS 1500 // each of the two startup color-bar checks; 0 disables
#endif
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ 40000000UL  // stable starting point; 80000000UL is optional, board-dependent
#endif
#ifndef GT_POKEMON_COPIES
#define GT_POKEMON_COPIES 1
#endif
#ifndef CYD_CACHE_KB
#define CYD_CACHE_KB 80
#endif
