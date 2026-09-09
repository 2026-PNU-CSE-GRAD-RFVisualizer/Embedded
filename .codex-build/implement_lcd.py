from pathlib import Path
p=Path('handheld_jpeg_stream/main/lcd_gpio_writer.c');s=p.read_text().replace('#include <stdbool.h>','#include <stdbool.h>\n#include <string.h>\n#include "esp_timer.h"\n#include "esp_system.h"')
s=s.replace('static uint16_t *dma_frame;', '''static uint16_t *dma_buffers[2];
static uint16_t *dma_frame;
static bool dma_pending;
static int64_t dma_started_us;
typedef struct {
    int64_t pack, gate, command, wait, transfer;
} lcd_timing_t;
static lcd_timing_t timing;

static void report_timing(int64_t started)
{
    static lcd_timing_t sums;
    static int64_t window_start, total_sum, total_max;
    static unsigned frames;
    const int64_t now = esp_timer_get_time();
    const int64_t total = now - started;
    if (!window_start) window_start = started;
    sums.pack += timing.pack; sums.gate += timing.gate;
    sums.command += timing.command; sums.wait += timing.wait;
    sums.transfer += timing.transfer;
    total_sum += total;
    if (total > total_max) total_max = total;
    ++frames;
    if (now - window_start >= 1000000) {
        const double divisor = frames * 1000.0;
        ESP_LOGI(TAG, "LCD fps=%.2f total_avg/max=%.2f/%.2f ms pack=%.2f gate=%.2f cmd=%.2f wait=%.2f dma_span=%.2f ms",
                 frames * 1000000.0 / (now - window_start),
                 total_sum / divisor, total_max / 1000.0, sums.pack / divisor,
                 sums.gate / divisor, sums.command / divisor, sums.wait / divisor,
                 sums.transfer / divisor);
        memset(&sums, 0, sizeof(sums));
        frames = 0; total_sum = total_max = 0; window_start = now;
    }
}
''')
s=s.replace('A 32-row internal-SRAM bounce buffer','An internal-SRAM bounce buffer')
s=s.replace('#define LCD_STRIPE_ROWS   32U','#define LCD_STRIPE_ROWS   CONFIG_HANDHELD_LCD_STRIPE_ROWS')
a=s.index('    dma_frame = esp_lcd_i80_alloc_draw_buffer(');b=s.index('    return ESP_OK;',a)
s=s[:a]+'''    const unsigned buffer_count =
#if CONFIG_HANDHELD_LCD_PING_PONG
        2;
#else
        1;
#endif
    for (unsigned i = 0; i < buffer_count; ++i) {
        dma_buffers[i] = esp_lcd_i80_alloc_draw_buffer(
            panel_io, LCD_STRIPE_BYTES,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(dma_buffers[i] != NULL, ESP_ERR_NO_MEM, TAG,
                            "DMA stripe allocation failed; largest internal block=%u",
                            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    }
    dma_frame = dma_buffers[0];
    ESP_LOGI(TAG, "I80 %u Hz: %u x %u-byte DMA buffers, %u rows; internal free=%u largest=%u",
             LCD_PIXEL_CLOCK_HZ, buffer_count, (unsigned)LCD_STRIPE_BYTES,
             (unsigned)LCD_STRIPE_ROWS,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
'''+s[b:]
a=s.index('static esp_err_t dma_send_packed_stripe(');b=s.index('\nesp_err_t lcd_gpio_writer_init',a)
s=s[:a]+'''/* There is at most one outstanding transfer. Only the other buffer may be
 * packed while it is active. The task that acquired the IMU mutex releases
 * it after completion; callbacks only signal completion (never give mutexes).
 * Parameter writes also drain the IDF queue, so queue depth >1 is unnecessary.
 */
static esp_err_t dma_finish_stripe(void)
{
    if (!dma_pending) return ESP_OK;
    const int64_t started = esp_timer_get_time();
    if (xSemaphoreTake(dma_done, pdMS_TO_TICKS(LCD_DMA_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "LCD DMA timeout; restarting before buffer or gate reuse");
        esp_restart();
        return ESP_ERR_TIMEOUT;
    }
    const int64_t now = esp_timer_get_time();
    timing.wait += now - started;
    timing.transfer += now - dma_started_us;
    dma_pending = false;
    leave_transfer_gate();
    taskYIELD();
    return ESP_OK;
}

static esp_err_t dma_send_packed_stripe(size_t stripe)
{
    ESP_RETURN_ON_ERROR(dma_finish_stripe(), TAG, "previous stripe failed");
    int64_t started = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(enter_transfer_gate(), TAG, "LCD gate failed");
    timing.gate += esp_timer_get_time() - started;
    const uint16_t y_start = (uint16_t)(stripe * LCD_STRIPE_ROWS);
    started = esp_timer_get_time();
    esp_err_t result = dma_set_window(0, y_start, LCD_H_RES - 1,
                                      y_start + LCD_STRIPE_ROWS - 1);
    timing.command += esp_timer_get_time() - started;
    if (result == ESP_OK) {
        dma_started_us = esp_timer_get_time();
        result = esp_lcd_panel_io_tx_color(
            panel_io, NT35510_RAMWR, dma_frame, LCD_STRIPE_BYTES);
    }
    if (result != ESP_OK) {
        leave_transfer_gate();
        return result;
    }
    dma_pending = true;
#if !CONFIG_HANDHELD_LCD_PING_PONG
    return dma_finish_stripe();
#else
    return ESP_OK;
#endif
}

static void select_pack_buffer(size_t stripe)
{
#if CONFIG_HANDHELD_LCD_PING_PONG
    dma_frame = dma_buffers[stripe % 2U];
#else
    (void)stripe;
    dma_frame = dma_buffers[0];
#endif
}
'''+s[b:]
# both full frame packing paths
for name,nextname in [('static esp_err_t dma_send_pixels(', 'void lcd_gpio_writer_set_transfer_gate('),('static esp_err_t draw_indexed_pixels(', 'esp_err_t lcd_gpio_writer_draw_rgb332(')]:
 a=s.index(name);b=s.index(nextname,a);f=s[a:b]
 f=f.replace('    for (size_t stripe = 0;', '    const int64_t frame_started = esp_timer_get_time();\n    memset(&timing, 0, sizeof(timing));\n    for (size_t stripe = 0;')
 f=f.replace('        const size_t input_base', '        select_pack_buffer(stripe);\n        const int64_t pack_started = esp_timer_get_time();\n        const size_t input_base')
 f=f.replace('        ESP_RETURN_ON_ERROR(dma_send_packed_stripe', '        timing.pack += esp_timer_get_time() - pack_started;\n        ESP_RETURN_ON_ERROR(dma_send_packed_stripe')
 f=f.replace('    return ESP_OK;','    ESP_RETURN_ON_ERROR(dma_finish_stripe(), TAG, "last stripe failed");\n    report_timing(frame_started);\n    return ESP_OK;')
 s=s[:a]+f+s[b:]
s=s.replace('static void pack_rgb666_pair','static inline void pack_rgb666_pair')
p.write_text(s)
