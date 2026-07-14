# Protocol

## ESP-NOW Binary Packet

ESP32 RSSI nodes send a packed binary packet to the ESP32 gateway.

```c
#define RSSI_PACKET_MAGIC   0x52465349u
#define RSSI_PACKET_VERSION 1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  node_id;
    uint16_t payload_len;
    uint32_t seq;
    uint32_t uptime_ms;
    uint8_t  ap_bssid[6];
    int8_t   rssi_raw_dbm;
    int16_t  rssi_filtered_x10;
    uint8_t  sample_count;
    uint16_t error_flags;
    uint32_t crc32;
} rssi_node_packet_t;
```

`crc32` is calculated over the whole struct while the `crc32` field is set to `0`.

Filtered RSSI is sent as fixed-point x10:

```text
-60.8 dBm -> -608
```

## UART Line Protocol

Gateway to STM32 uses an NMEA-style line protocol.

```text
$RSSI,<node_id>,<seq>,<uptime_ms>,<rssi_raw>,<rssi_filtered_x10>,<sample_count>,<error_flags>*<checksum>\n
$GWSTAT,<uptime_ms>,<rx_count>,<crc_error_count>,<queue_drop_count>*<checksum>\n
```

The checksum is XOR of all bytes after `$` and before `*`, printed as two uppercase hex characters.

Example payload used for checksum:

```text
RSSI,1,15234,3600123,-62,-608,5,0
```

## STM32 Preprocessed MQTT Payload

The STM32-side code can turn the latest node table into an MQTT-ready JSON payload.

Topic recommendation:

```text
rfmap/stm32/{device_id}/rssi_snapshot
```

Payload example:

```json
{
  "schema_version": 1,
  "device_id": "stm32-rssi-bridge-01",
  "uptime_ms": 3605000,
  "node_count": 4,
  "nodes": [
    {"node_id":1,"seq":15234,"age_ms":20,"rssi_raw_dbm":-62,"rssi_filtered_x10":-608,"sample_count":5,"error_flags":0}
  ]
}
```

Actual MQTT publishing is intentionally left outside this module because the STM32 network path is board-specific.
