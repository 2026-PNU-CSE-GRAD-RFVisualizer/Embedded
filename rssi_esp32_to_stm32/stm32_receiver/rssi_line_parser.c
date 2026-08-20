#include "rssi_line_parser.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static char s_line_buf[RSSI_LINE_MAX_LEN];
static uint16_t s_line_len;
static uint8_t s_in_frame;

static size_t bounded_strlen(const char *text, size_t max_len)
{
    size_t len = 0;
    while (len < max_len && text[len] != '\0') {
        len++;
    }
    return len;
}

uint8_t rssi_line_checksum_payload(const char *payload)
{
    uint8_t checksum = 0;
    while (*payload != '\0') {
        checksum ^= (uint8_t)*payload++;
    }
    return checksum;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    if (text[0] == '-') {
        return 0;
    }
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xFFFFFFFFul) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static int parse_u64(const char *text, uint64_t *out)
{
    char *end = NULL;
    if (text[0] == '-') {
        return 0;
    }

    unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || value > UINT64_MAX) {
        return 0;
    }
    *out = (uint64_t)value;
    return 1;
}

static int parse_i32(const char *text, int32_t *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return 0;
    }
    *out = (int32_t)value;
    return 1;
}

void rssi_parser_init(void)
{
    memset(s_line_buf, 0, sizeof(s_line_buf));
    s_line_len = 0;
    s_in_frame = 0;
}

rssi_parse_result_t rssi_parser_feed_byte(uint8_t byte, rssi_measurement_t *out_measurement)
{
    if (byte == '$') {
        s_in_frame = 1;
        s_line_len = 0;
        s_line_buf[s_line_len++] = (char)byte;
        return RSSI_PARSE_INCOMPLETE;
    }

    if (!s_in_frame) {
        return RSSI_PARSE_INCOMPLETE;
    }

    if (byte == '\r') {
        return RSSI_PARSE_INCOMPLETE;
    }

    if (s_line_len >= RSSI_LINE_MAX_LEN - 1u) {
        s_in_frame = 0;
        s_line_len = 0;
        return RSSI_PARSE_FORMAT_ERROR;
    }

    s_line_buf[s_line_len++] = (char)byte;

    if (byte == '\n') {
        s_line_buf[s_line_len] = '\0';
        s_in_frame = 0;
        s_line_len = 0;
        return rssi_parse_line(s_line_buf, out_measurement);
    }

    return RSSI_PARSE_INCOMPLETE;
}

rssi_parse_result_t rssi_parse_line(const char *line, rssi_measurement_t *out_measurement)
{
    if (line == NULL || out_measurement == NULL || line[0] != '$') {
        return RSSI_PARSE_FORMAT_ERROR;
    }

    char local[RSSI_LINE_MAX_LEN];
    size_t len = bounded_strlen(line, sizeof(local));
    if (len == 0 || len >= sizeof(local)) {
        return RSSI_PARSE_FORMAT_ERROR;
    }

    memcpy(local, line, len + 1u);
    while (len > 0 && (local[len - 1u] == '\n' || local[len - 1u] == '\r')) {
        local[--len] = '\0';
    }

    char *star = strchr(local, '*');
    if (star == NULL || star[1] == '\0' || star[2] == '\0' || star[3] != '\0') {
        return RSSI_PARSE_FORMAT_ERROR;
    }

    int high = hex_value(star[1]);
    int low = hex_value(star[2]);
    if (high < 0 || low < 0) {
        return RSSI_PARSE_FORMAT_ERROR;
    }

    *star = '\0';
    const char *payload = &local[1];
    uint8_t expected = (uint8_t)((high << 4) | low);
    uint8_t actual = rssi_line_checksum_payload(payload);
    if (actual != expected) {
        return RSSI_PARSE_CHECKSUM_ERROR;
    }

    char *fields[9] = {0};
    uint8_t field_count = 0;
    int extra_field = 0;
    char *cursor = (char *)payload;
    while (cursor != NULL && field_count < 9u) {
        fields[field_count++] = cursor;
        char *comma = strchr(cursor, ',');
        if (comma == NULL) {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
        if (field_count == 9u && cursor[0] != '\0') {
            extra_field = 1;
        }
    }

    if (field_count != 9u || extra_field || strcmp(fields[0], "RSSI") != 0) {
        return RSSI_PARSE_FORMAT_ERROR;
    }

    uint32_t u32 = 0;
    int32_t i32 = 0;
    rssi_measurement_t parsed = {0};

    if (!parse_u32(fields[1], &u32) || u32 > 255u) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    parsed.node_id = (uint8_t)u32;

    if (!parse_u32(fields[2], &parsed.seq)) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    if (!parse_u32(fields[3], &parsed.uptime_ms)) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    if (!parse_u64(fields[4], &parsed.measurement_timestamp_ms)) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    if (!parse_i32(fields[5], &i32) || i32 < -128 || i32 > 127) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    parsed.rssi_raw_dbm = (int8_t)i32;

    if (!parse_i32(fields[6], &i32) || i32 < -32768 || i32 > 32767) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    parsed.rssi_filtered_x10 = (int16_t)i32;

    if (!parse_u32(fields[7], &u32) || u32 > 255u) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    parsed.sample_count = (uint8_t)u32;

    if (!parse_u32(fields[8], &u32) || u32 > 65535u) {
        return RSSI_PARSE_FORMAT_ERROR;
    }
    parsed.error_flags = (uint16_t)u32;

    *out_measurement = parsed;
    return RSSI_PARSE_OK;
}
