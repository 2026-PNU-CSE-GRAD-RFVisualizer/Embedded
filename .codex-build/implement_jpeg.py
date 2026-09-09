from pathlib import Path
p=Path('handheld_jpeg_stream/main/CMakeLists.txt')
s=p.read_text(); block='if(CONFIG_HANDHELD_RGB565_BENCHMARK)\n    target_add_binary_data(${COMPONENT_LIB} "test_animation/frames/frame_00.jpg" BINARY)\nendif()\n'
s=s.replace(block,''); s+='\n'+block;p.write_text(s)
p=Path('handheld_jpeg_stream/main/jpeg_lcd_sink.c');s=p.read_text();s=s.replace('#include "jpeg_decoder.h"','#if CONFIG_HANDHELD_NEW_JPEG\n#include "esp_jpeg_dec.h"\n#else\n#include "jpeg_decoder.h"\n#endif\n#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"')
s=s.replace('static uint8_t *s_jpeg_work_buffer;','#if !CONFIG_HANDHELD_NEW_JPEG\nstatic uint8_t *s_jpeg_work_buffer;\n#endif')
s=s.replace('    /* esp_jpeg\'s automatic','    #if !CONFIG_HANDHELD_NEW_JPEG\n    /* esp_jpeg\'s automatic').replace('    ESP_RETURN_ON_ERROR(lcd_gpio_writer_init()', '    #endif\n\n    ESP_RETURN_ON_ERROR(lcd_gpio_writer_init()')
s=s.replace('"RGB565 buffer=%u bytes in PSRAM, JPEG work=%u bytes internal",','"RGB565 buffer=%u bytes in PSRAM, new_decoder=%u",').replace('(unsigned)JPEG_WORK_BUFFER_BYTES);\n    return ESP_OK;','(unsigned)CONFIG_HANDHELD_NEW_JPEG);\n    return ESP_OK;')
# Kconfig disabled bool has no C macro: use local preprocessor constant
s=s.replace('(unsigned)CONFIG_HANDHELD_NEW_JPEG','\n#if CONFIG_HANDHELD_NEW_JPEG\n             1U\n#else\n             0U\n#endif\n             ')
start=s.index('    esp_jpeg_image_cfg_t info_config')
s=s[:start]+'''#if CONFIG_HANDHELD_NEW_JPEG
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t decoder = NULL;
    const int64_t start_us = esp_timer_get_time();
    jpeg_error_t status = jpeg_dec_open(&config, &decoder);
    ESP_RETURN_ON_FALSE(status == JPEG_ERR_OK, ESP_FAIL, TAG,
                        "JPEG open failed: %d", status);
    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)frame->jpeg,
        .inbuf_len = (int)frame->jpeg_length,
        .outbuf = (uint8_t *)s_rgb565_frame,
    };
    jpeg_dec_header_info_t header = {0};
    esp_err_t result = ESP_FAIL;
    status = jpeg_dec_parse_header(decoder, &io, &header);
    if (status != JPEG_ERR_OK) {
        goto decode_done;
    }
    int required = 0;
    status = jpeg_dec_get_outbuf_len(decoder, &required);
    if (status != JPEG_ERR_OK || required <= 0 ||
        (size_t)required > RGB565_FRAME_BYTES ||
        header.width == 0 || header.height == 0 ||
        header.width > LCD_H_RES || header.height > LCD_V_RES) {
        result = ESP_ERR_INVALID_SIZE;
        goto decode_done;
    }
    status = jpeg_dec_process(decoder, &io);
    if (status == JPEG_ERR_OK) {
        if (io.out_size != (int)((size_t)header.width * header.height * 2U)) {
            result = ESP_ERR_INVALID_SIZE;
            goto decode_done;
        }
        if (header.width != LCD_H_RES || header.height != LCD_V_RES) {
            upscale_rgb565_in_place(header.width, header.height);
        }
        result = ESP_OK;
    }
decode_done:
    jpeg_dec_close(decoder);
    *decode_elapsed_us = esp_timer_get_time() - start_us;
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "seq=%lu JPEG failed: codec=%d result=%s",
                 (unsigned long)frame->seq, status, esp_err_to_name(result));
    }
    return result;
#else
'''+s[start:]
s=s.replace('    esp_jpeg_image_cfg_t info_config', '    const int64_t decode_start_us = esp_timer_get_time();\n    esp_jpeg_image_cfg_t info_config')
s=s.replace('    const int64_t decode_start_us = esp_timer_get_time();\n    result = esp_jpeg_decode','    result = esp_jpeg_decode').replace('    *decode_elapsed_us = esp_timer_get_time() - decode_start_us;\n','')
marker='    return ESP_OK;\n}\n\nesp_err_t jpeg_lcd_sink_render'
s=s.replace(marker,'    *decode_elapsed_us = esp_timer_get_time() - decode_start_us;\n    return ESP_OK;\n#endif\n}\n\nesp_err_t jpeg_lcd_sink_render')
s=s.replace('ESP_LOGI(TAG, "displayed','ESP_LOGD(TAG, "displayed').replace('ESP_LOGI(TAG, "seq=%lu scaled','ESP_LOGD(TAG, "seq=%lu scaled')
# periodic decoder statistics
s=s.replace('    ESP_LOGD(TAG, "displayed', '''    static int64_t window_start, decode_sum, decode_max, draw_sum, draw_max;
    static unsigned count;
    if (!window_start) window_start = draw_start_us - decode_elapsed_us;
    decode_sum += decode_elapsed_us;
    draw_sum += draw_elapsed_us;
    if (decode_elapsed_us > decode_max) decode_max = decode_elapsed_us;
    if (draw_elapsed_us > draw_max) draw_max = draw_elapsed_us;
    ++count;
    const int64_t now = esp_timer_get_time();
    if (now - window_start >= 1000000) {
        ESP_LOGI(TAG, "JPEG decode_avg/max=%.2f/%.2f ms draw_avg/max=%.2f/%.2f ms n=%u",
                 decode_sum / (count * 1000.0), decode_max / 1000.0,
                 draw_sum / (count * 1000.0), draw_max / 1000.0, count);
        window_start = now;
        count = 0;
        decode_sum = decode_max = draw_sum = draw_max = 0;
    }
    ESP_LOGD(TAG, "displayed''')
s+='''
#if CONFIG_HANDHELD_RGB565_BENCHMARK
void jpeg_lcd_sink_benchmark(void)
{
    extern const uint8_t start[] asm("_binary_frame_00_jpg_start");
    extern const uint8_t end[] asm("_binary_frame_00_jpg_end");
    const jpeg_stream_frame_t frame = {.jpeg = start, .jpeg_length = end - start};
    int64_t decode_us;
    ESP_ERROR_CHECK(decode_rgb565(&frame, &decode_us));
    ESP_LOGI(TAG, "RGB565 LCD-only benchmark: decoded once, no pacing");
    for (;;) {
        ESP_ERROR_CHECK(lcd_gpio_writer_draw(s_rgb565_frame, RGB565_PIXEL_COUNT));
        taskYIELD();
    }
}
#endif
'''
p.write_text(s)
p=Path('handheld_jpeg_stream/main/jpeg_lcd_sink.h');s=p.read_text();s+='\nvoid jpeg_lcd_sink_benchmark(void);\n';p.write_text(s)
for file in ['app_main.c']:
 p=Path('handheld_jpeg_stream/main')/file;s=p.read_text().replace('CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST || CONFIG_HANDHELD_CONTROL_UDP','CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST || CONFIG_HANDHELD_CONTROL_UDP || CONFIG_HANDHELD_BNO085_SERVICE');s=s.replace('#if CONFIG_HANDHELD_LOCAL_10FPS_TEST\n    const bool','#if CONFIG_HANDHELD_LOCAL_10FPS_TEST || CONFIG_HANDHELD_RGB565_BENCHMARK\n    const bool');s=s.replace('    // show_boot_color_test();','    // show_boot_color_test();');s=s.replace('#if CONFIG_HANDHELD_LOCAL_10FPS_TEST\n    ESP_LOGI', '#if CONFIG_HANDHELD_RGB565_BENCHMARK\n    jpeg_lcd_sink_benchmark();\n#endif\n\n#if CONFIG_HANDHELD_LOCAL_10FPS_TEST\n    ESP_LOGI');p.write_text(s)
p=Path('handheld_jpeg_stream/main/jpeg_stream_client.c');s=p.read_text().replace('ESP_LOGI(TAG, "received','ESP_LOGD(TAG, "received');p.write_text(s)
p=Path('handheld_jpeg_stream/main/rgb332_zlib_sink.c');s=p.read_text().replace('ESP_LOGI(TAG,\n             "displayed','ESP_LOGD(TAG,\n             "displayed');p.write_text(s)
