#pragma once

#include <stdint.h>

/* Exact RGB565 -> panel byte conversion, split into disjoint contributions.
 * 2 KiB of internal RAM replaces channel expansion on every displayed pixel.
 * Initialize once before use. This does not quantize or change pixel order.
 */
typedef struct {
    uint32_t high[256];
    uint32_t low[256];
} lcd_rgb666_lookup_t;

static inline void lcd_rgb666_lookup_init(lcd_rgb666_lookup_t *table)
{
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned red5 = i >> 3;
        const unsigned blue5 = i & 31U;
        const unsigned red = ((red5 << 3) | (red5 >> 2)) & 0xFCU;
        const unsigned blue = ((blue5 << 3) | (blue5 >> 2)) & 0xFCU;
        table->high[i] = (red << 16) | ((i & 7U) << 13);
        table->low[i] = ((i & 0xE0U) << 5) | blue;
    }
}

static inline void lcd_rgb666_pack_pair(const lcd_rgb666_lookup_t *table,
                                        uint16_t first, uint16_t second,
                                        uint16_t output[3])
{
    const uint32_t a = table->high[first >> 8] | table->low[first & 255U];
    const uint32_t b = table->high[second >> 8] | table->low[second & 255U];
    output[0] = (uint16_t)(a >> 8);
    output[1] = (uint16_t)((a << 8) | (b >> 16));
    output[2] = (uint16_t)b;
}
