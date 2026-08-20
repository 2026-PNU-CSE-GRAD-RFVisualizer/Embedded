# Protocol

## ESP-NOW Binary Packet

ESP32 RSSI nodes send a packed binary packet to the ESP32 gateway.

```c
#define RSSI_PACKET_MAGIC   0x52465349u
#define RSSI_PACKET_VERSION 2

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  node_id;
    uint16_t payload_len;
    uint32_t seq;
    uint32_t uptime_ms;
    uint64_t measurement_timestamp_ms;
    uint8_t  ap_bssid[6];
    int8_t   rssi_raw_dbm;
    int16_t  rssi_filtered_x10;
    uint8_t  sample_count;
    uint16_t error_flags;
    uint32_t crc32;
} rssi_node_packet_t;
```

`crc32` is calculated over the whole struct while the `crc32` field is set to `0`.

`measurement_timestamp_ms` is the RSSI measurement time as Unix epoch milliseconds,
captured by the originating node. It is `0` and `RSSI_ERR_TIME_INVALID` is set until
that node has synchronized its clock. `uptime_ms` remains a separate boot-relative
diagnostic value.

Version 1 and version 2 packets have different sizes. Nodes and the gateway must be
updated together; the gateway intentionally rejects version 1 after this change.

Filtered RSSI is sent as fixed-point x10:

```text
-60.8 dBm -> -608
```

## UART Line Protocol

Gateway to STM32 uses an NMEA-style line protocol.

```text
$RSSI,<node_id>,<seq>,<uptime_ms>,<measurement_timestamp_ms>,<rssi_raw>,<rssi_filtered_x10>,<sample_count>,<error_flags>*<checksum>\n
$GWSTAT,<uptime_ms>,<rx_count>,<crc_error_count>,<queue_drop_count>*<checksum>\n
```

The checksum is XOR of all bytes after `$` and before `*`, printed as two uppercase hex characters.

Example payload used for checksum:

```text
RSSI,1,15234,3600123,1785720000123,-62,-608,5,0
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
  "timestamp": 1785720000400,
  "readings": [
    {"node_id":"node-01","timestamp":1785720000123,"rssi":-61,"seq":15234,"rssi_raw":-62,"status":0}
  ]
}
```

The top-level `timestamp` is the snapshot creation time. Each reading's `timestamp`
is the originating node's measurement time and must be used for measurement-time
windowing. The formatter accepts the STM32 monotonic uptime separately so timeout
tracking never mixes uptime with Unix time.

Actual MQTT publishing is intentionally left outside this module because the STM32 network path is board-specific.
