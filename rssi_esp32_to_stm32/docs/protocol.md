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
  "schema_version": 2,
  "gateway_id": "gw-01",
  "timestamp": 3605000,
  "active_node_count": 4,
  "rx_count": 15240,
  "accepted_count": 15234,
  "checksum_errors": 0,
  "format_errors": 0,
  "uart_overflows": 0,
  "readings": [
    {"node_id":"node-01","rssi":-61,"rssi_x10":-608,"rssi_raw":-62,"seq":15234,"age_ms":20,"valid_age_ms":20,"valid":true,"timed_out":false,"status":0}
  ]
}
```

`valid=false` means the latest packet did not contain a usable measurement (for
example `AP_NOT_FOUND`) or the last valid RSSI is stale. Consumers must not use
that entry for positioning. `timed_out=true` separately indicates that no packet
has arrived from the node within the communication timeout.

Actual MQTT publishing is intentionally left outside this module because the STM32 network path is board-specific.
