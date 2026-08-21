#include "mqtt_payload.h"

#include <stdarg.h>
#include <stdio.h>

static int append_text(char *out, size_t out_len, size_t *offset, const char *fmt, ...)
{
    if (*offset >= out_len) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(&out[*offset], out_len - *offset, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= out_len - *offset) {
        return -1;
    }

    *offset += (size_t)written;
    return 0;
}

static int filtered_rssi_dbm(int16_t filtered_x10)
{
    if (filtered_x10 >= 0) {
        return (filtered_x10 + 5) / 10;
    }

    return (filtered_x10 - 5) / 10;
}

static int append_node_id(char *out, size_t out_len, size_t *offset, uint8_t node_id)
{
    if (node_id == 5u) {
        return append_text(out, out_len, offset, "\"gw-01\"");
    }

    return append_text(out, out_len, offset, "\"node-%02u\"", node_id);
}

int mqtt_payload_build_snapshot(char *out,
                                size_t out_len,
                                const char *gateway_id,
                                const rssi_preprocessor_t *ctx,
                                uint32_t now_uptime_ms,
                                uint64_t snapshot_timestamp_ms)
{
    if (out == NULL || out_len == 0u || gateway_id == NULL || ctx == NULL) {
        return -1;
    }

    size_t offset = 0;

    if (append_text(out,
                    out_len,
                    &offset,
                    "{\"schema_version\":2,\"gateway_id\":\"%s\",\"timestamp\":%llu,\"readings\":[",
                    gateway_id,
                    (unsigned long long)snapshot_timestamp_ms) != 0) {
        return -1;
    }

    uint8_t emitted = 0;
    for (uint8_t i = 0; i < RSSI_MAX_NODES; ++i) {
        const stm32_node_state_t *node = &ctx->nodes[i];
        if (!node->active || node->timed_out) {
            continue;
        }

        uint32_t age_ms = now_uptime_ms - node->last_update_ms;
        (void)age_ms;
        if (append_text(out,
                        out_len,
                        &offset,
                        "%s{\"node_id\":",
                        emitted > 0u ? "," : "") != 0) {
            return -1;
        }

        if (append_node_id(out, out_len, &offset, node->node_id) != 0) {
            return -1;
        }

        if (append_text(out,
                        out_len,
                        &offset,
                        ",\"timestamp\":%llu,\"rssi\":%d,\"seq\":%lu,\"rssi_raw\":%d,\"status\":%u}",
                        (unsigned long long)node->measurement_timestamp_ms,
                        filtered_rssi_dbm(node->last_filtered_x10),
                        (unsigned long)node->last_seq,
                        node->last_raw_rssi,
                        node->error_flags) != 0) {
            return -1;
        }
        emitted++;
    }

    if (append_text(out, out_len, &offset, "]}") != 0) {
        return -1;
    }

    return (int)offset;
}
