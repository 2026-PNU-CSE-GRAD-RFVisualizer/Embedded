#ifndef RSSI_LINE_PARSER_H
#define RSSI_LINE_PARSER_H

#include <stdint.h>

#define RSSI_LINE_MAX_LEN       128
#define RSSI_MAX_NODES          8

typedef struct {
    uint8_t node_id;
    uint32_t seq;
    uint32_t uptime_ms;
    uint64_t measurement_timestamp_ms;
    int8_t rssi_raw_dbm;
    int16_t rssi_filtered_x10;
    uint8_t sample_count;
    uint16_t error_flags;
} rssi_measurement_t;

typedef enum {
    RSSI_PARSE_OK = 0,
    RSSI_PARSE_INCOMPLETE,
    RSSI_PARSE_CHECKSUM_ERROR,
    RSSI_PARSE_FORMAT_ERROR
} rssi_parse_result_t;

void rssi_parser_init(void);

rssi_parse_result_t rssi_parser_feed_byte(uint8_t byte, rssi_measurement_t *out_measurement);
rssi_parse_result_t rssi_parse_line(const char *line, rssi_measurement_t *out_measurement);
uint8_t rssi_line_checksum_payload(const char *payload);

#endif
