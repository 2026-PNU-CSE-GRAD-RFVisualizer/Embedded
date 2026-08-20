#include <stdio.h>
#include <string.h>

#include "mqtt_payload.h"
#include "rssi_line_parser.h"
#include "rssi_preprocessor.h"

static void make_line(char *out, size_t out_len, const char *payload)
{
    unsigned int checksum = rssi_line_checksum_payload(payload);
    snprintf(out, out_len, "$%s*%02X\n", payload, checksum);
}

int main(void)
{
    char line[RSSI_LINE_MAX_LEN];
    rssi_measurement_t measurement;
    rssi_preprocessor_t ctx;
    char payload[MQTT_PAYLOAD_MAX_LEN];

    rssi_parser_init();
    rssi_preprocessor_init(&ctx);

    make_line(line,
              sizeof(line),
              "RSSI,1,15234,3600123,1785720000123,-62,-608,5,0");
    rssi_parse_result_t result = rssi_parse_line(line, &measurement);
    if (result != RSSI_PARSE_OK) {
        printf("valid parse failed: %d\n", result);
        return 1;
    }
    if (measurement.measurement_timestamp_ms != 1785720000123ULL) {
        printf("measurement timestamp parse failed\n");
        return 1;
    }

    if (!rssi_preprocessor_update(&ctx, &measurement, 1000)) {
        printf("preprocessor update failed\n");
        return 1;
    }

    make_line(line,
              sizeof(line),
              "RSSI,2,10,2000,1785720000200,-70,-695,5,0");
    for (size_t i = 0; i < strlen(line); ++i) {
        result = rssi_parser_feed_byte((uint8_t)line[i], &measurement);
        if (result == RSSI_PARSE_OK) {
            (void)rssi_preprocessor_update(&ctx, &measurement, 1100);
        }
    }

    strcpy(line, "$RSSI,3,1,1000,1785720000300,-50,-500,5,0*00\n");
    result = rssi_parse_line(line, &measurement);
    if (result != RSSI_PARSE_CHECKSUM_ERROR) {
        printf("bad checksum accepted: %d\n", result);
        return 1;
    }

    rssi_preprocessor_update_timeouts(&ctx, 1200);
    int len = mqtt_payload_build_snapshot(payload,
                                          sizeof(payload),
                                          "gw-01",
                                          &ctx,
                                          1200,
                                          1785720000400ULL);
    if (len <= 0) {
        printf("mqtt payload build failed\n");
        return 1;
    }

    if (strstr(payload, "\"schema_version\":2") == NULL ||
        strstr(payload, "\"timestamp\":1785720000400") == NULL ||
        strstr(payload, "\"node_id\":\"node-01\",\"timestamp\":1785720000123") == NULL ||
        strstr(payload, "\"node_id\":\"node-02\",\"timestamp\":1785720000200") == NULL) {
        printf("timestamp missing from MQTT payload: %s\n", payload);
        return 1;
    }

    printf("%s\n", payload);
    printf("OK\n");
    return 0;
}
