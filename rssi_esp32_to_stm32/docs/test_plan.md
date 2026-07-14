# Test Plan

## 1. STM32 Parser Host Test

Build and run:

```bash
cd stm32_receiver
gcc -std=c99 -Wall -Wextra -pedantic rssi_line_parser.c rssi_preprocessor.c mqtt_payload.c test_parser_host.c -o test_parser_host
./test_parser_host
```

Expected:

- Valid `$RSSI` line parses successfully.
- Bad checksum line is rejected.
- Node table updates per `node_id`.
- MQTT-ready JSON payload is generated.

## 2. Gateway Fake Output

Build and flash `esp32_gateway`.

Before nodes are ready, enable or call the fake packet path in `app_main.c` during bring-up if needed. The final gateway path prints lines only when ESP-NOW packets arrive.

Expected UART line:

```text
$RSSI,1,1,1000,-60,-600,5,0*XX
```

## 3. Gateway as Local Node 5

```text
Gateway ESP32 -> STM32
```

Enable:

```c
#define GATEWAY_LOCAL_RSSI_ENABLE 1
#define GATEWAY_LOCAL_NODE_ID 5
```

Expected:

- STM32 receives `$RSSI,5,...`.
- Gateway serial log shows `local node=5`.
- MQTT-ready JSON includes node 5.

## 4. Single Remote Node

```text
ESP32 Node 1 -> ESP32 Gateway -> PC serial or STM32 UART
```

Expected:

- Node logs raw and filtered RSSI.
- Gateway receives valid ESP-NOW packets.
- Gateway forwards `$RSSI` lines.
- STM32 parser accepts lines.

## 5. Multi Node

```text
ESP32 Node 1..4 -> ESP32 Gateway/Node 5 -> STM32
```

Expected:

- Node IDs 1..5 remain distinct.
- Sequence loss is counted by the gateway and STM32 preprocessor.
- No UART line corruption at 115200 baud with 1 Hz/node.

## 6. Fault Injection

Test:

- Power off target AP.
- Power off one node.
- Reboot gateway.
- Disconnect UART briefly.

Expected:

- Missing AP sets `RSSI_ERR_AP_NOT_FOUND`.
- Node timeouts are visible on STM32 side.
- Parser resynchronizes after malformed or partial lines.
