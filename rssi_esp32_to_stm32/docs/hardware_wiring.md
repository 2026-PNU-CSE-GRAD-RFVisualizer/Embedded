# Hardware Wiring

## ESP32 Nodes

With five ESP32 boards total, use four remote RSSI nodes and one gateway that also acts as local node 5.

Remote RSSI nodes only need USB power for the first prototype.

Configure each node with:

- Unique `NODE_ID`
- Same `ESPNOW_WIFI_CHANNEL`
- Same `TARGET_AP_BSSID`
- Same `TARGET_AP_CHANNEL`
- Gateway ESP-NOW MAC address

Recommended IDs:

```text
Remote ESP32 nodes: NODE_ID 1, 2, 3, 4
Gateway ESP32:      GATEWAY_LOCAL_NODE_ID 5
```

## ESP32 Gateway to STM32 UART

Default gateway pins:

```text
ESP32 GPIO17 TX -> STM32 UART RX
ESP32 GPIO18 RX <- STM32 UART TX, optional for later ACK support
ESP32 GND       -> STM32 GND
```

Both boards must use 3.3 V UART logic.

The gateway also scans the target AP and forwards its own measurement as `$RSSI,5,...`.

## Channel Rule

ESP-NOW uses the active Wi-Fi channel. Wi-Fi scan can temporarily affect the channel and reduce ESP-NOW reliability.

For the first prototype:

```text
target AP channel == ESP-NOW channel
```

If the target AP channel is unknown, scan once with a phone or PC Wi-Fi analyzer, then write the channel into `node_config.h`.
