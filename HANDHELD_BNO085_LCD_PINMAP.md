# ESP32-S3 Handheld BNO085 + NT35510 Pin Map

## 기준 하드웨어

- ESP32-S3-DevKitC-1-N16R8
- BNO085 IMU breakout
- Waveshare 4-inch 480×800 LCD
- NT35510, 16-bit 8080 parallel interface
- LCD logic level: 3.3 V

## NT35510 LCD

| LCD pin | ESP32-S3 pin | Note |
|---|---:|---|
| D0 | GPIO38 | GPIO0 boot-strapping 문제 회피 |
| D1 | GPIO1 | Data bus |
| D2 | GPIO2 | Data bus |
| D3 | GPIO3 | Data bus |
| D4 | GPIO4 | Data bus |
| D5 | GPIO5 | Data bus |
| D6 | GPIO6 | Data bus |
| D7 | GPIO7 | Data bus |
| D8 | GPIO8 | Data bus |
| D9 | GPIO9 | Data bus |
| D10 | GPIO10 | Data bus |
| D11 | GPIO11 | Data bus |
| D12 | GPIO12 | Data bus |
| D13 | GPIO13 | Data bus |
| D14 | GPIO14 | Data bus |
| D15 | GPIO15 | Data bus |
| WR | GPIO16 | Write strobe |
| DC / RS | GPIO18 | Data/Command select |
| CS | GPIO21 | Chip select |
| RST | GPIO47 | LCD reset |
| BL | GPIO48 | Backlight enable |
| RD | 3.3 V에 고정 | 현재 Firmware는 write-only bus 사용 |
| 5V | 5 V | LCD module 전원 |
| BL_VCC | 5 V | Backlight 전원 |
| GND | GND | ESP32-S3 및 BNO085와 공통 접지 |
| XPT2046 touch pins | 연결하지 않음 | 현재 통합 시험 범위 아님 |

## BNO085

| BNO085 pin | ESP32-S3 pin | DevKitC-1 header | Note |
|---|---:|---:|---|
| SDA | GPIO39 | J3-9 | I2C data |
| SCL | GPIO40 | J3-8 | I2C clock |
| INT / INTN | GPIO41 | J3-7 | Active-low data-ready interrupt |
| RST / RESETN | GPIO42 | J3-6 | Active-low hardware reset |
| 3V3 / VCC | 3.3 V | J1-1 또는 J1-2 | Breakout 전원 사양 확인 |
| GND | GND | J3-1, J3-21 또는 J3-22 | 공통 접지 |
| PS0 / PS1 | I2C mode | Breakout별 상이 | Reset 전에 I2C mode로 설정 |
| SA0 | `0x4A` 또는 `0x4B` 선택 | Breakout별 상이 | 실제 ACK 주소에 맞게 Firmware 설정 |

## 연결 주의사항

- 이 핀맵은 `handheld_jpeg_stream/main/lcd_board_config.h`의 현재 LCD 배선을 기준으로 한다.
- 구형 `handheld_lcd_test`의 `D0 → GPIO0`, `RD → GPIO17` 배선과 혼용하지 않는다.
- 현재 통합 배선에서는 `LCD D0 → GPIO38`, `LCD RD → 3.3 V`를 사용한다.
- LCD module의 대체 전원 입력 두 개를 동시에 연결하지 않는다.
- LCD와 Backlight에는 안정적인 5 V 전원을 사용하고 ESP32-S3, LCD, BNO085의 GND를 공통으로 연결한다.
- 정확한 사양을 확인하지 않은 BNO085 breakout의 `VIN`에 5 V를 연결하지 않는다.
- BNO085의 I2C, INT 및 RESET 신호는 3.3 V logic이어야 한다.
- SDA와 SCL에 pull-up이 없는 breakout은 3.3 V로 약 2.2–4.7 kΩ pull-up을 추가한다.
- LCD parallel data/control 배선은 가능한 한 짧게 유지한다.

## GPIO 사용 요약

```text
LCD    : GPIO1~16, GPIO18, GPIO21, GPIO38, GPIO47, GPIO48
BNO085 : GPIO39, GPIO40, GPIO41, GPIO42
Unused by this pin map: GPIO0, GPIO17
```
