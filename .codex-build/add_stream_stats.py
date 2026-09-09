from pathlib import Path
p=Path('handheld_jpeg_stream/main/app_main.c');s=p.read_text();s=s.replace('    esp_err_t result;\n    if (frame->flags', '''    static int64_t window_start;
    static uint32_t displayed, failed;
    static jpeg_stream_client_stats_t previous;
    const int64_t started = esp_timer_get_time();
    if (!window_start) window_start = started;
    esp_err_t result;
    if (frame->flags''')
s=s.replace('    if (result != ESP_OK) {\n        ESP_LOGW(TAG, "frame seq=', '''    if (result == ESP_OK) ++displayed;
    else ++failed;
    const int64_t now = esp_timer_get_time();
    if (now - window_start >= 1000000) {
        jpeg_stream_client_stats_t current;
        jpeg_stream_client_get_stats(&current);
        const double seconds = (now - window_start) / 1000000.0;
        ESP_LOGI(TAG, "stream rx_fps=%.2f display_fps=%.2f stale=%lu gaps=%lu failed=%lu",
                 (current.frames_received - previous.frames_received) / seconds,
                 displayed / seconds,
                 (unsigned long)(current.stale_frames_dropped - previous.stale_frames_dropped),
                 (unsigned long)(current.sequence_gaps - previous.sequence_gaps),
                 (unsigned long)failed);
        previous = current;
        displayed = failed = 0;
        window_start = now;
    }
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "frame seq=''')
if '#include "esp_timer.h"' not in s:s=s.replace('#include "esp_log.h"','#include "esp_log.h"\n#include "esp_timer.h"')
p.write_text(s)
p=Path('handheld_jpeg_stream/main/jpeg_lcd_sink.c');s=p.read_text();s=s.replace('    #if !CONFIG_HANDHELD_NEW_JPEG','#if !CONFIG_HANDHELD_NEW_JPEG').replace('    #endif','#endif');s=s.replace('    ESP_LOGI(TAG, "LCD ready:', '''#if CONFIG_HANDHELD_NEW_JPEG
    const char *decoder_name = "esp_new_jpeg 1.0.2";
#else
    const char *decoder_name = "esp_jpeg 1.3.1";
#endif
    ESP_LOGI(TAG, "LCD ready:''');a=s.index('             "RGB565 buffer=');b=s.index('    return ESP_OK;',a);s=s[:a]+'''             "RGB565 buffer=%u bytes in PSRAM, decoder=%s",
             LCD_H_RES, LCD_V_RES, (unsigned)RGB565_FRAME_BYTES, decoder_name);
'''+s[b:];p.write_text(s)
