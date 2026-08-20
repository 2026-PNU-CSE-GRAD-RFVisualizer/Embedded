#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t seq;
    uint64_t timestamp_ms;
    uint8_t flags;
    const uint8_t *jpeg;
    size_t jpeg_length;
} jpeg_stream_frame_t;

typedef void (*jpeg_stream_frame_callback_t)(const jpeg_stream_frame_t *frame,
                                              void *user_context);

typedef struct {
    const char *server_host;
    uint16_t server_port;
    size_t max_frame_bytes;
    uint32_t receive_timeout_ms;
    uint32_t reconnect_initial_ms;
    uint32_t reconnect_max_ms;
    jpeg_stream_frame_callback_t on_frame;
    void *user_context;
} jpeg_stream_client_config_t;

typedef struct {
    uint32_t frames_received;
    uint32_t stale_frames_dropped;
    uint32_t sequence_gaps;
    uint32_t invalid_jpegs;
    uint32_t invalid_payloads;
    uint32_t stream_errors;
    uint32_t reconnects;
} jpeg_stream_client_stats_t;

esp_err_t jpeg_stream_client_start(const jpeg_stream_client_config_t *config);
void jpeg_stream_client_get_stats(jpeg_stream_client_stats_t *out);
