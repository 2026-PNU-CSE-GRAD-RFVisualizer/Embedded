#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mqtt_payload.h"
#include "rssi_line_parser.h"
#include "rssi_preprocessor.h"
#include "uart_rx_ring.h"

#define CHECK(condition, message)          \
    do {                                   \
        if (!(condition)) {                \
            printf("FAIL: %s\n", message); \
            return 1;                      \
        }                                  \
    } while (0)

static void make_line(char *out, size_t out_len, const char *payload)
{
    unsigned int checksum = rssi_line_checksum_payload(payload);
    snprintf(out, out_len, "$%s*%02X\n", payload, checksum);
}

static rssi_measurement_t measurement(uint8_t node_id,
                                      uint32_t seq,
                                      uint32_t uptime_ms,
                                      int8_t raw,
                                      int16_t filtered_x10,
                                      uint16_t flags)
{
    rssi_measurement_t value = {
        .node_id = node_id,
        .seq = seq,
        .uptime_ms = uptime_ms,
        .rssi_raw_dbm = raw,
        .rssi_filtered_x10 = filtered_x10,
        .sample_count = 5u,
        .error_flags = flags,
    };
    return value;
}

static stm32_node_state_t *find_node(rssi_preprocessor_t *ctx, uint8_t node_id)
{
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        if (ctx->nodes[i].active && ctx->nodes[i].node_id == node_id) {
            return &ctx->nodes[i];
        }
    }
    return NULL;
}

static int test_parser(void)
{
    char line[RSSI_LINE_MAX_LEN];
    rssi_measurement_t parsed;

    make_line(line, sizeof(line), "RSSI,1,15234,3600123,-62,-608,5,0");
    CHECK(rssi_parse_line(line, &parsed) == RSSI_PARSE_OK, "valid line rejected");
    CHECK(parsed.seq == 15234u && parsed.rssi_filtered_x10 == -608,
          "valid line fields differ");

    make_line(line, sizeof(line), "RSSI,1,-1,3600123,-62,-608,5,0");
    CHECK(rssi_parse_line(line, &parsed) == RSSI_PARSE_FORMAT_ERROR,
          "negative unsigned sequence accepted");

    strcpy(line, "$RSSI,3,1,1000,-50,-500,5,0*00\n");
    CHECK(rssi_parse_line(line, &parsed) == RSSI_PARSE_CHECKSUM_ERROR,
          "bad checksum accepted");
    return 0;
}

static int test_preprocessor(void)
{
    rssi_preprocessor_t ctx;
    rssi_preprocessor_init(&ctx);

    rssi_measurement_t item = measurement(1u, 100u, 100000u, -62, -608, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &item, 1000u) == RSSI_UPDATE_ACCEPTED,
          "first measurement not accepted");

    stm32_node_state_t *node = find_node(&ctx, 1u);
    CHECK(node != NULL && node->last_filtered_x10 == -608, "node state missing");

    item.rssi_filtered_x10 = -300;
    CHECK(rssi_preprocessor_update(&ctx, &item, 1100u) == RSSI_UPDATE_DUPLICATE,
          "duplicate not classified");
    CHECK(node->last_filtered_x10 == -608 && node->accepted_count == 1u,
          "duplicate overwrote accepted state");
    CHECK(node->packet_rx_count == 2u && node->duplicate_count == 1u,
          "duplicate counters incorrect");

    item.seq = 98u;
    item.uptime_ms = 98000u;
    CHECK(rssi_preprocessor_update(&ctx, &item, 1200u) == RSSI_UPDATE_OUT_OF_ORDER,
          "out-of-order packet not classified");
    CHECK(node->last_seq == 100u && node->out_of_order_count == 1u,
          "out-of-order packet overwrote latest sequence");

    item = measurement(1u, 103u, 103000u, -60, -590, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &item, 1300u) == RSSI_UPDATE_ACCEPTED,
          "new packet not accepted");
    CHECK(node->lost_count == 2u && node->last_filtered_x10 == -590,
          "sequence gap not counted");

    item = measurement(1u, 104u, 104000u, -40, -400, RSSI_ERR_AP_NOT_FOUND);
    CHECK(rssi_preprocessor_update(&ctx, &item, 1400u) == RSSI_UPDATE_INVALID_MEASUREMENT,
          "AP failure not classified invalid");
    CHECK(node->last_filtered_x10 == -590 && node->last_valid_update_ms == 1300u,
          "invalid measurement overwrote last valid RSSI");
    CHECK(!rssi_preprocessor_node_rssi_valid(node) && node->invalid_count == 1u,
          "invalid measurement remained usable");

    item = measurement(1u, 105u, 105000u, -59, -585, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &item, 1500u) == RSSI_UPDATE_ACCEPTED,
          "valid measurement did not recover state");

    item = measurement(1u, 90u, 90000u, -70, -700, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &item, 1550u) == RSSI_UPDATE_OUT_OF_ORDER,
          "delayed old packet misclassified as reboot");

    item = measurement(1u, 1u, 1000u, -58, -580, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &item, 1600u) == RSSI_UPDATE_REBOOTED,
          "node reboot not detected");
    CHECK(node->last_seq == 1u && node->reboot_count == 1u,
          "rebooted sequence not accepted");

    rssi_measurement_t wrap = measurement(2u, UINT32_MAX, 200000u, -70, -700, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &wrap, 1700u) == RSSI_UPDATE_ACCEPTED,
          "wrap test initial packet failed");
    wrap.seq = 0u;
    wrap.uptime_ms++;
    CHECK(rssi_preprocessor_update(&ctx, &wrap, 1800u) == RSSI_UPDATE_ACCEPTED,
          "sequence wrap-around rejected");

    rssi_preprocessor_update_timeouts(&ctx, 4601u);
    CHECK(node->communication_timed_out && node->rssi_stale,
          "communication/data timeout not applied");

    item = measurement(0u, 1u, 1000u, -60, -600, 0u);
    CHECK(rssi_preprocessor_update(&ctx, &item, 4700u) == RSSI_UPDATE_REJECTED,
          "invalid node id accepted");
    return 0;
}

static int test_json_capacity(void)
{
    rssi_preprocessor_t ctx;
    char payload[MQTT_PAYLOAD_MAX_LEN];
    rssi_preprocessor_init(&ctx);

    for (uint8_t node_id = 1u; node_id <= RSSI_MAX_NODES; ++node_id) {
        rssi_measurement_t item = measurement(node_id,
                                              UINT32_MAX - node_id,
                                              100000u + node_id,
                                              -60,
                                              (int16_t)(-600 - node_id),
                                              0u);
        CHECK(rssi_preprocessor_update(&ctx, &item, 1000u + node_id) == RSSI_UPDATE_ACCEPTED,
              "eight-node setup failed");
        stm32_node_state_t *node = find_node(&ctx, node_id);
        node->packet_rx_count = UINT32_MAX;
        node->accepted_count = UINT32_MAX;
        node->valid_count = UINT32_MAX;
        node->invalid_count = UINT32_MAX;
        node->lost_count = UINT32_MAX;
        node->duplicate_count = UINT32_MAX;
        node->out_of_order_count = UINT32_MAX;
        node->reboot_count = UINT32_MAX;
    }

    rssi_preprocessor_update_timeouts(&ctx, 1200u);
    int len = mqtt_payload_build_snapshot(payload, sizeof(payload), "gw-01", &ctx, 1200u);
    CHECK(len > 0 && (size_t)len < sizeof(payload), "eight-node JSON buffer too small");
    CHECK(strstr(payload, "\"schema_version\":2") != NULL, "schema version missing");
    CHECK(strstr(payload, "\"rssi_x10\":") != NULL, "x10 RSSI missing");
    CHECK(strstr(payload, "\"valid\":true") != NULL, "valid state missing");
    return 0;
}

static int test_uart_ring(void)
{
    uart_rx_ring_t ring;
    uart_rx_ring_init(&ring);

    for (uint16_t i = 0u; i < UART_RX_RING_CAPACITY - 1u; ++i) {
        CHECK(uart_rx_ring_push_isr(&ring, (uint8_t)i), "ring filled too early");
    }
    CHECK(!uart_rx_ring_push_isr(&ring, 0u), "ring overflow not detected");
    CHECK(uart_rx_ring_overflow_count(&ring) == 1u, "ring overflow count incorrect");

    for (uint16_t i = 0u; i < UART_RX_RING_CAPACITY - 1u; ++i) {
        uint8_t value = 0u;
        CHECK(uart_rx_ring_pop(&ring, &value), "ring pop failed");
        CHECK(value == (uint8_t)i, "ring byte order changed");
    }
    return 0;
}

int main(void)
{
    rssi_parser_init();
    CHECK(test_parser() == 0, "parser tests failed");
    CHECK(test_preprocessor() == 0, "preprocessor tests failed");
    CHECK(test_json_capacity() == 0, "JSON tests failed");
    CHECK(test_uart_ring() == 0, "UART ring tests failed");
    printf("OK\n");
    return 0;
}
