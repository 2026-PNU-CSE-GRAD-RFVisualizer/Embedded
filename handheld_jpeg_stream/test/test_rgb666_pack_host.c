#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lcd_rgb666_pack.h"

/* Independent copy of the original channel arithmetic, kept as the oracle. */
static void original(uint16_t a, uint16_t b, uint16_t out[3])
{
    uint8_t bytes[6];
    const uint16_t pixels[2] = {a, b};
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned r = (pixels[i] >> 11) & 31;
        const unsigned g = (pixels[i] >> 5) & 63;
        const unsigned blue = pixels[i] & 31;
        bytes[3*i] = ((r << 3) | (r >> 2)) & 0xFC;
        bytes[3*i+1] = ((g << 2) | (g >> 4)) & 0xFC;
        bytes[3*i+2] = ((blue << 3) | (blue >> 2)) & 0xFC;
    }
    for (unsigned i = 0; i < 3; ++i)
        out[i] = ((uint16_t)bytes[2*i] << 8) | bytes[2*i+1];
}

static void check(const lcd_rgb666_lookup_t *table, uint16_t a, uint16_t b)
{
    uint16_t expected[3];
    uint16_t guarded[5] = {0x1234, 0, 0, 0, 0xABCD};
    original(a, b, expected);
    lcd_rgb666_pack_pair(table, a, b, guarded+1);
    assert(memcmp(expected, guarded+1, sizeof(expected)) == 0);
    assert(guarded[0] == 0x1234 && guarded[4] == 0xABCD);
}

int main(void)
{
    lcd_rgb666_lookup_t table;
    lcd_rgb666_lookup_init(&table);
    /* Exhaust all 65,536 colors in both positions. Each output contribution
     * depends on only one pixel; these sweeps cover every contribution. */
    for (uint32_t c = 0; c < 65536; ++c) {
        check(&table, (uint16_t)c, 0xA55A);
        check(&table, 0x5AA5, (uint16_t)c);
        check(&table, (uint16_t)c, (uint16_t)~c);
    }
    uint32_t rng = 0x12345678;
    for (unsigned i = 0; i < 100000; ++i) {
        rng = rng * 1664525U + 1013904223U;
        check(&table, (uint16_t)rng, (uint16_t)(rng >> 16));
    }
    puts("RGB666: all 65536 colors in both positions + complementary/random pairs match; guards intact");
    return 0;
}
