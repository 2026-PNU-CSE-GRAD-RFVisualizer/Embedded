# ESP32-S3 + Waveshare 4-inch LCD smoke test

This is a standalone ESP-IDF project for:

- ESP32-S3-DevKitC-1-N8R8
- Waveshare 4inch Resistive Touch LCD
- NT35510 panel revision identified by DB=0x80/DC=0x00, 480x800, RGB565
- 16-bit Intel 8080 parallel bus

It initializes the panel, cycles through solid red/green/blue screens, and then
draws the embedded `testimage.rgb565`. Pixel data is currently sent by a slow
direct-GPIO writer so the physical 16-bit bus can be verified without ESP LCD
DMA byte-packing ambiguity.
The source JPEG is converted to 480x800 RGB565 on the development PC, so this
smoke test does not require a JPEG decoder on the ESP32-S3. It does not use
Arduino, LVGL, or the XPT2046 touch controller.

## Wiring

| LCD | ESP32-S3 |
|---|---|
| 5V | 5V |
| GND | GND |
| D0 ... D15 | GPIO0 ... GPIO15 in order |
| LCD_CS | GPIO21 |
| DC | GPIO18 |
| WR | GPIO16 |
| RD | GPIO17 |
| RST | GPIO47 |
| BL_VCC | 5V |
| BL | GPIO48 |
| Touch pins | Not connected for this test |

Do not connect both of the LCD module's alternative power inputs at the same
time. LCD logic signals are 3.3 V. Keep the parallel wires short, ideally under
10 cm.

## Build and flash in the VS Code ESP-IDF extension

1. Open `handheld_lcd_test` as the ESP-IDF project directory.
2. Run `ESP-IDF: Set Espressif Device Target` and choose `esp32s3`.
3. Run `ESP-IDF: Select Port to Use` and select the board's COM port.
4. Run `ESP-IDF: Build your project`.
5. Run `ESP-IDF: Flash your project`.
6. Run `ESP-IDF: Monitor your device`.

Equivalent terminal commands from an ESP-IDF terminal are:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the actual port.

## Expected result

The monitor should contain:

```text
LCD ID raw reads: DA=..../.... DB=..../.... DC=..../.... (dummy/value)
readback after init: MADCTL=..../.... COLMOD=..../0055 (dummy/value, RGB565 value=0055)
direct GPIO solid red test
direct GPIO solid green test
direct GPIO solid blue test
drawing embedded 480x800 RGB565 test image (768000 bytes)
direct GPIO embedded image complete
```

The display should show solid red, green, and blue for one second each, followed
by the embedded flower test image.

## Troubleshooting

- White screen with backlight: check CS, DC, WR, RST, and D0-D15 order.
- Upload does not start: power off/disconnect the LCD during upload because
  GPIO0 is a boot-strapping pin.
- Flickering or corrupted pixels: shorten the D0-D15, WR, DC, and CS wires and
  recheck that every data wire is connected in numerical order.
- A magenta image that fills exactly 2/3 of the panel means the controller is
  still interpreting RAM data as 18-bit pixels. Check that the final COLMOD
  value in the monitor is `0055`; if it is not, recheck D0, D2, D4, D6, WR,
  and DC continuity.
- Solid red and blue are exchanged: set `LCD_SWAP_COLOR_BYTES` to `1`.
- Repeated resets: use a stable 5 V supply for the LCD backlight and keep a
  common ground with the ESP32-S3.

The pin map is intentionally kept in `main/lcd_board_config.h` so it can be
changed without editing the display driver.
