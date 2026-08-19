#pragma once

#include <stddef.h>
#include <stdint.h>

#define JPEG_STREAM_MAGIC       0x52464A46u /* 'RFJF' */
#define JPEG_STREAM_VERSION     1u
#define JPEG_STREAM_HEADER_SIZE 22u

typedef struct {
    uint8_t flags;
    uint32_t seq;
    uint64_t timestamp_ms;
    uint32_t payload_length;
} jpeg_stream_header_t;

typedef enum {
    JPEG_STREAM_HEADER_OK = 0,
    JPEG_STREAM_HEADER_BAD_ARGUMENT,
    JPEG_STREAM_HEADER_BAD_MAGIC,
    JPEG_STREAM_HEADER_BAD_VERSION,
    JPEG_STREAM_HEADER_TOO_LARGE,
} jpeg_stream_header_result_t;

jpeg_stream_header_result_t jpeg_stream_parse_header(
    const uint8_t header[JPEG_STREAM_HEADER_SIZE],
    size_t max_payload_bytes,
    jpeg_stream_header_t *out);

const char *jpeg_stream_header_result_name(jpeg_stream_header_result_t result);

