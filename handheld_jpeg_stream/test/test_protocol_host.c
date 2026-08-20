#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jpeg_stream_protocol.h"

static void write_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void write_be64(uint8_t *p, uint64_t value)
{
    write_be32(p, (uint32_t)(value >> 32));
    write_be32(p + 4, (uint32_t)value);
}

static void make_header(uint8_t *header, uint8_t flags, uint32_t length)
{
    memset(header, 0, JPEG_STREAM_HEADER_SIZE);
    write_be32(header, JPEG_STREAM_MAGIC);
    header[4] = JPEG_STREAM_VERSION;
    header[5] = flags;
    write_be32(header + 6, 42);
    write_be64(header + 10, UINT64_C(1785720000000));
    write_be32(header + 18, length);
}

int main(void)
{
    uint8_t raw[JPEG_STREAM_HEADER_SIZE];
    jpeg_stream_header_t parsed;
    make_header(raw, JPEG_STREAM_FLAG_RGB332_ZLIB, 123456);

    assert(jpeg_stream_parse_header(raw, 524288, &parsed) ==
           JPEG_STREAM_HEADER_OK);
    assert(parsed.flags == JPEG_STREAM_FLAG_RGB332_ZLIB);
    assert(parsed.seq == 42);
    assert(parsed.timestamp_ms == UINT64_C(1785720000000));
    assert(parsed.payload_length == 123456);

    raw[0] = 0;
    assert(jpeg_stream_parse_header(raw, 524288, &parsed) ==
           JPEG_STREAM_HEADER_BAD_MAGIC);

    make_header(raw, JPEG_STREAM_FLAG_JPEG, 123456);
    raw[4] = 2;
    assert(jpeg_stream_parse_header(raw, 524288, &parsed) ==
           JPEG_STREAM_HEADER_BAD_VERSION);

    make_header(raw, JPEG_STREAM_FLAG_JPEG, 524289);
    assert(jpeg_stream_parse_header(raw, 524288, &parsed) ==
           JPEG_STREAM_HEADER_TOO_LARGE);

    puts("4/4 protocol tests passed");
    return 0;
}
