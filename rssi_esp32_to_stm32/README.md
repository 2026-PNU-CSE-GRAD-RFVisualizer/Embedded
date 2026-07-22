# ESP32 RSSI Nodes to STM32 Prototype

This repository contains the first embedded prototype for the 3DGS RF mapping project.

Goal:

```text
ESP32 RSSI Node 1..4 --ESP-NOW--> ESP32 Gateway + Node 5 --UART--> STM32
STM32 parser/preprocessor --> MQTT-ready JSON payload
```

This phase intentionally does not implement LCD, IMU, JPEG streaming, or the 3D viewer path.

## Repository Layout

```text
rssi_esp32_to_stm32/
├── docs/
├── esp32_node/
├── esp32_gateway/
└── stm32_receiver/
```

## Hardware

- 4 ESP32 boards as remote fixed RSSI nodes
- 1 ESP32 board as ESP-NOW to UART gateway and local RSSI node 5
- 1 STM32 board receiving UART lines
- Target AP with a fixed BSSID and known Wi-Fi channel

UART wiring:

```text
ESP32 Gateway TX  -> STM32 UART RX
ESP32 Gateway GND -> STM32 GND
Logic level       -> 3.3 V
```

Default gateway UART settings:

```text
UART:     UART1
Baudrate: 115200
TX GPIO:  17
RX GPIO:  18
```

## ESP32 Node Setup

Edit `esp32_node/main/node_config.h` for each node:

- `NODE_ID`
- `TARGET_AP_USE_BSSID`
- `TARGET_AP_SSID`
- `TARGET_AP_BSSID`
- `TARGET_AP_CHANNEL`
- `ESPNOW_WIFI_CHANNEL`
- `GATEWAY_ESPNOW_MAC`

Use the same Wi-Fi channel for the target AP and ESP-NOW during the first prototype.
The current deployment scans `U1MU2_2F` only on channel 6 to avoid ESP-NOW channel hopping.

Build and flash:

```bash
cd esp32_node
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

For ESP32-S3 nodes:

```bash
idf.py set-target esp32s3
```

## ESP32 Gateway Setup

Edit `esp32_gateway/main/gateway_config.h` if UART pins or channel need to change.

The default deployment uses the gateway as local RSSI node 5:

```c
#define GATEWAY_LOCAL_RSSI_ENABLE    1
#define GATEWAY_LOCAL_NODE_ID        5
#define TARGET_AP_USE_BSSID          0
#define TARGET_AP_SSID               "U1MU2_2F"
#define TARGET_AP_BSSID              {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define ESPNOW_WIFI_CHANNEL          6
#define TARGET_AP_CHANNEL            ESPNOW_WIFI_CHANNEL
```

All nodes and the gateway must use the same fixed channel. If `U1MU2_2F` moves away from channel 6, update `ESPNOW_WIFI_CHANNEL` in both config headers before flashing every board.

With five ESP32 boards total, flash:

```text
ESP32 #1..#4: esp32_node firmware, NODE_ID 1..4
ESP32 #5:     esp32_gateway firmware, GATEWAY_LOCAL_NODE_ID 5
```

If only one ESP32 is available, set:

```c
#define GATEWAY_FAKE_RSSI_TEST 1
#define GATEWAY_FAKE_NODE_COUNT 4
#define GATEWAY_LOCAL_RSSI_ENABLE 1
#define GATEWAY_LOCAL_NODE_ID 5
```

Then flash the gateway firmware. It will generate fake `$RSSI` lines for node IDs 1 to 4 and real local Wi-Fi RSSI as node ID 5. This lets one ESP32 act as a gateway plus local node before the other ESP32 boards arrive.

For a no-STM32 test, just use `idf.py monitor`. The gateway writes the same `$RSSI,...*XX` lines to UART1 and also prints them through ESP-IDF logging, so the USB serial monitor is enough to confirm values:

```text
$RSSI,1,...   fake remote node
$RSSI,2,...   fake remote node
$RSSI,3,...   fake remote node
$RSSI,4,...   fake remote node
$RSSI,5,...   real local Wi-Fi RSSI
```

Build and flash:

```bash
cd esp32_gateway
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

The gateway forwards each valid node packet to STM32 as:

```text
$RSSI,<node_id>,<seq>,<uptime_ms>,<rssi_raw>,<rssi_filtered_x10>,<sample_count>,<error_flags>*<checksum>
```

Example:

```text
$RSSI,1,15234,3600123,-62,-608,5,0*5A
```

## STM32 Integration

Copy these files into the STM32 project:

- `stm32_receiver/rssi_line_parser.c`
- `stm32_receiver/rssi_line_parser.h`
- `stm32_receiver/rssi_preprocessor.c`
- `stm32_receiver/rssi_preprocessor.h`
- `stm32_receiver/mqtt_payload.c`
- `stm32_receiver/mqtt_payload.h`
- `stm32_receiver/uart_rx_ring.c`
- `stm32_receiver/uart_rx_ring.h`

Use `stm32_uart_receiver_example.c` as a HAL UART interrupt integration example.

The STM32 parser has no dynamic allocation. It verifies the line checksum, parses `$RSSI` messages, updates a node table, and can produce an MQTT-ready JSON payload string.

## Host Parser Test

On a PC with a C compiler:

```bash
cd stm32_receiver
gcc -std=c99 -Wall -Wextra -pedantic rssi_line_parser.c rssi_preprocessor.c mqtt_payload.c uart_rx_ring.c test_parser_host.c -o test_parser_host
./test_parser_host
```

On Windows with MinGW, the output executable may be `test_parser_host.exe`.

## Known Limitations

1. ESP-NOW and Wi-Fi scanning are channel-sensitive.
2. The first prototype assumes the target AP channel and ESP-NOW channel are identical.
3. RSSI is unstable indoors and should not be treated as direct distance.
4. STM32 board-specific UART and clock setup must be generated separately in CubeMX or an existing HAL project.
5. The STM32 code currently creates MQTT-ready payload text; actual MQTT transport depends on the network module selected later.
