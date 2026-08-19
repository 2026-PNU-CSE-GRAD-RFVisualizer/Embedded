#include "jpeg_stream_protocol.h"

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p)
{
    return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

jpeg_stream_header_result_t jpeg_stream_parse_header(
    const uint8_t header[JPEG_STREAM_HEADER_SIZE],
    size_t max_payload_bytes,
    jpeg_stream_header_t *out)
{
    if (header == NULL || out == NULL || max_payload_bytes == 0) {
        return JPEG_STREAM_HEADER_BAD_ARGUMENT;
    }
    if (read_be32(header) != JPEG_STREAM_MAGIC) {
        return JPEG_STREAM_HEADER_BAD_MAGIC;
    }
    if (header[4] != JPEG_STREAM_VERSION) {
        return JPEG_STREAM_HEADER_BAD_VERSION;
    }

    const uint32_t payload_length = read_be32(header + 18);
    if ((size_t)payload_length > max_payload_bytes) {
        return JPEG_STREAM_HEADER_TOO_LARGE;
    }

    out->flags = header[5];
    out->seq = read_be32(header + 6);
    out->timestamp_ms = read_be64(header + 10);
    out->payload_length = payload_length;
    return JPEG_STREAM_HEADER_OK;
}

const char *jpeg_stream_header_result_name(jpeg_stream_header_result_t result)
{
    switch (result) {
    case JPEG_STREAM_HEADER_OK: return "ok";
    case JPEG_STREAM_HEADER_BAD_ARGUMENT: return "bad_argument";
    case JPEG_STREAM_HEADER_BAD_MAGIC: return "bad_magic";
    case JPEG_STREAM_HEADER_BAD_VERSION: return "bad_version";
    case JPEG_STREAM_HEADER_TOO_LARGE: return "too_large";
    default: return "unknown";
    }
}
