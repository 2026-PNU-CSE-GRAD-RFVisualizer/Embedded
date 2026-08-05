#pragma once

/*
 * ESP32-S3-DevKitC-1 -> Waveshare 4inch NT35510 (16-bit 8080)
 *
 * LCD D0..D15 are connected in order to GPIO0..GPIO15.
 * GPIO0 is a strapping pin. The LCD data bus must not drive it during reset.
 */
#define LCD_PIN_D0          0
#define LCD_PIN_D1          1
#define LCD_PIN_D2          2
#define LCD_PIN_D3          3
#define LCD_PIN_D4          4
#define LCD_PIN_D5          5
#define LCD_PIN_D6          6
#define LCD_PIN_D7          7
#define LCD_PIN_D8          8
#define LCD_PIN_D9          9
#define LCD_PIN_D10        10
#define LCD_PIN_D11        11
#define LCD_PIN_D12        12
#define LCD_PIN_D13        13
#define LCD_PIN_D14        14
#define LCD_PIN_D15        15

#define LCD_PIN_WR         16
#define LCD_PIN_RD         17
#define LCD_PIN_DC         18
#define LCD_PIN_CS         21
#define LCD_PIN_RST        47
#define LCD_PIN_BL         48

#define LCD_BL_ON_LEVEL     1
/* Start slowly when using long Dupont wires. Raise only after stable output. */
#define LCD_PIXEL_CLOCK_HZ  (2 * 1000 * 1000)

#define LCD_H_RES          480
#define LCD_V_RES          800
#define LCD_TILE_LINES      20
#define LCD_BUS_WIDTH       16

/* A 16-bit bus transfers one complete RGB565 pixel per WR pulse. */
#define LCD_SWAP_COLOR_BYTES 0
