#include "nt35510.h"

#include <stdbool.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define NT35510_CASET   0x2A00
#define NT35510_PASET   0x2B00
#define NT35510_RAMWR   0x2C00
#define NT35510_RAMWRC  0x3C00
#define NT35510_MADCTL  0x3600
#define NT35510_COLMOD  0x3A00
#define NT35510_TEON    0x3500
#define NT35510_SLPOUT  0x1100
#define NT35510_DISPON  0x2900

static const char *TAG = "nt35510";

struct nt35510_t {
    esp_lcd_panel_io_handle_t io;
    SemaphoreHandle_t transfer_done;
    bool frame_started;
};

static bool color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *event_data,
                                void *user_ctx)
{
    (void)panel_io;
    (void)event_data;
    nt35510_handle_t lcd = user_ctx;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(lcd->transfer_done, &task_woken);
    return task_woken == pdTRUE;
}

static esp_err_t write_reg(nt35510_handle_t lcd, uint16_t reg, uint8_t value)
{
    /* NT35510 register parameters occupy the low byte of the 16-bit bus. */
    const uint16_t parameter = value;
    return esp_lcd_panel_io_tx_param(lcd->io, reg, &parameter, sizeof(parameter));
}

static esp_err_t write_regs(nt35510_handle_t lcd, uint16_t first_reg,
                            const uint8_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(write_reg(lcd, first_reg + i, values[i]), TAG,
                            "register 0x%04x failed", first_reg + (unsigned)i);
    }
    return ESP_OK;
}

static esp_err_t send_command(nt35510_handle_t lcd, uint16_t command)
{
    return esp_lcd_panel_io_tx_param(lcd->io, command, NULL, 0);
}

static esp_err_t hardware_reset(int reset_gpio)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << reset_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "reset GPIO config failed");

    gpio_set_level(reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(reset_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(reset_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    return ESP_OK;
}

static esp_err_t controller_init(nt35510_handle_t lcd)
{
    /*
     * Initialization values follow the NT35510 480x800 sequence used by
     * Waveshare's 4inch Resistive Touch LCD example and Arduino_GFX.
     */
    static const uint8_t page1[] = {0x55, 0xAA, 0x52, 0x08, 0x01};
    static const uint8_t avdd_ratio[] = {0x34, 0x34, 0x34};
    static const uint8_t avdd[] = {0x0D, 0x0D, 0x0D};
    static const uint8_t avee_ratio[] = {0x34, 0x34, 0x34};
    static const uint8_t avee[] = {0x0D, 0x0D, 0x0D};
    static const uint8_t vcl_ratio[] = {0x24, 0x24, 0x24};
    static const uint8_t vgh_ratio[] = {0x34, 0x34, 0x34};
    static const uint8_t vgh[] = {0x0F, 0x0F, 0x0F};
    static const uint8_t vgl_ratio[] = {0x24, 0x24, 0x24};
    static const uint8_t vgl[] = {0x08, 0x08};
    static const uint8_t vgmp[] = {0x00, 0x78, 0x00};
    static const uint8_t vgmn[] = {0x00, 0x78, 0x00};
    static const uint8_t vcom[] = {0x00, 0x89};
    static const uint8_t gamma[] = {
        0x00, 0x2D, 0x00, 0x2E, 0x00, 0x32, 0x00, 0x44, 0x00, 0x53,
        0x00, 0x88, 0x00, 0xB6, 0x00, 0xF3, 0x01, 0x22, 0x01, 0x64,
        0x01, 0x92, 0x01, 0xD4, 0x02, 0x07, 0x02, 0x08, 0x02, 0x34,
        0x02, 0x5F, 0x02, 0x78, 0x02, 0x94, 0x02, 0xA6, 0x02, 0xBB,
        0x02, 0xCA, 0x02, 0xDB, 0x02, 0xE8, 0x02, 0xF9, 0x03, 0x1F,
        0x03, 0x7F,
    };
    static const uint8_t page0[] = {0x55, 0xAA, 0x52, 0x08, 0x00};
    static const uint8_t rgb_if[] = {0x08, 0x05, 0x02, 0x05, 0x02};
    static const uint8_t gate_eq[] = {0x00, 0x00};
    static const uint8_t source_eq[] = {0x01, 0x05, 0x05, 0x05};
    static const uint8_t inversion[] = {0x00, 0x00, 0x00};
    static const uint8_t boe[] = {0x03, 0x00, 0x00};
    static const uint8_t timing[] = {0x01, 0x84, 0x07, 0x31, 0x00, 0x01};
    static const uint8_t command2[] = {0xAA, 0x55, 0x25, 0x01};

#define WRITE_ARRAY(reg, values) \
    ESP_RETURN_ON_ERROR(write_regs(lcd, (reg), (values), sizeof(values)), \
                        TAG, "init block 0x%04x failed", (reg))

    WRITE_ARRAY(0xF000, page1);
    WRITE_ARRAY(0xB600, avdd_ratio);
    WRITE_ARRAY(0xB000, avdd);
    WRITE_ARRAY(0xB700, avee_ratio);
    WRITE_ARRAY(0xB100, avee);
    WRITE_ARRAY(0xB800, vcl_ratio);
    WRITE_ARRAY(0xB900, vgh_ratio);
    WRITE_ARRAY(0xB300, vgh);
    WRITE_ARRAY(0xBA00, vgl_ratio);
    WRITE_ARRAY(0xB500, vgl);
    WRITE_ARRAY(0xBC00, vgmp);
    WRITE_ARRAY(0xBD00, vgmn);
    WRITE_ARRAY(0xBE00, vcom);

    WRITE_ARRAY(0xD100, gamma);
    WRITE_ARRAY(0xD400, gamma);
    WRITE_ARRAY(0xD200, gamma);
    WRITE_ARRAY(0xD500, gamma);
    WRITE_ARRAY(0xD300, gamma);
    WRITE_ARRAY(0xD600, gamma);

    WRITE_ARRAY(0xF000, page0);
    WRITE_ARRAY(0xB000, rgb_if);
    ESP_RETURN_ON_ERROR(write_reg(lcd, 0xB600, 0x08), TAG, "B600 failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, 0xB500, 0x50), TAG, "B500 failed");
    WRITE_ARRAY(0xB700, gate_eq);
    WRITE_ARRAY(0xB800, source_eq);
    WRITE_ARRAY(0xBC00, inversion);
    WRITE_ARRAY(0xCC00, boe);
    WRITE_ARRAY(0xBD00, timing);
    WRITE_ARRAY(0xFF00, command2);

#undef WRITE_ARRAY

    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_TEON, 0x00), TAG, "TEON failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_COLMOD, 0x55), TAG,
                        "RGB565 mode failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_MADCTL, 0x00), TAG,
                        "orientation failed");
    ESP_RETURN_ON_ERROR(send_command(lcd, NT35510_SLPOUT), TAG,
                        "sleep-out failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(send_command(lcd, NT35510_DISPON), TAG,
                        "display-on failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t nt35510_new(esp_lcd_panel_io_handle_t io, int reset_gpio,
                      nt35510_handle_t *out_lcd)
{
    ESP_RETURN_ON_FALSE(io && out_lcd, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");

    nt35510_handle_t lcd = calloc(1, sizeof(*lcd));
    ESP_RETURN_ON_FALSE(lcd, ESP_ERR_NO_MEM, TAG, "LCD allocation failed");

    lcd->io = io;
    lcd->transfer_done = xSemaphoreCreateBinary();
    if (!lcd->transfer_done) {
        free(lcd);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = hardware_reset(reset_gpio);
    if (err == ESP_OK) {
        err = controller_init(lcd);
    }
    if (err != ESP_OK) {
        nt35510_delete(lcd);
        return err;
    }

    *out_lcd = lcd;
    ESP_LOGI(TAG, "NT35510 initialized in 480x800 RGB565 mode");
    return ESP_OK;
}

static esp_err_t set_window(nt35510_handle_t lcd,
                            uint16_t x_start, uint16_t y_start,
                            uint16_t x_end, uint16_t y_end)
{
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_CASET + 0, x_start >> 8), TAG,
                        "x start MSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_CASET + 1, x_start & 0xFF), TAG,
                        "x start LSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_CASET + 2, x_end >> 8), TAG,
                        "x end MSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_CASET + 3, x_end & 0xFF), TAG,
                        "x end LSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_PASET + 0, y_start >> 8), TAG,
                        "y start MSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_PASET + 1, y_start & 0xFF), TAG,
                        "y start LSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_PASET + 2, y_end >> 8), TAG,
                        "y end MSB failed");
    ESP_RETURN_ON_ERROR(write_reg(lcd, NT35510_PASET + 3, y_end & 0xFF), TAG,
                        "y end LSB failed");
    return ESP_OK;
}

esp_err_t nt35510_draw_bitmap(nt35510_handle_t lcd,
                              uint16_t x_start, uint16_t y_start,
                              uint16_t x_end, uint16_t y_end,
                              const uint16_t *pixels)
{
    ESP_RETURN_ON_FALSE(lcd && pixels, ESP_ERR_INVALID_ARG, TAG,
                        "invalid argument");
    ESP_RETURN_ON_FALSE(x_start < x_end && y_start < y_end,
                        ESP_ERR_INVALID_ARG, TAG, "empty rectangle");

    ESP_RETURN_ON_ERROR(set_window(lcd, x_start, y_start,
                                   x_end - 1, y_end - 1), TAG,
                        "set window failed");

    const size_t bytes = (size_t)(x_end - x_start) *
                         (size_t)(y_end - y_start) * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(lcd->io, NT35510_RAMWR,
                                                   pixels, bytes),
                        TAG, "pixel transfer failed");

    ESP_RETURN_ON_FALSE(xSemaphoreTake(lcd->transfer_done, pdMS_TO_TICKS(1000)),
                        ESP_ERR_TIMEOUT, TAG, "pixel transfer timeout");
    return ESP_OK;
}

esp_err_t nt35510_begin_frame(nt35510_handle_t lcd)
{
    ESP_RETURN_ON_FALSE(lcd, ESP_ERR_INVALID_ARG, TAG, "invalid LCD handle");
    ESP_RETURN_ON_ERROR(set_window(lcd, 0, 0, 479, 799), TAG,
                        "full-screen window failed");
    lcd->frame_started = false;
    return ESP_OK;
}

esp_err_t nt35510_write_frame_chunk(nt35510_handle_t lcd,
                                    const uint16_t *pixels,
                                    size_t pixel_count)
{
    ESP_RETURN_ON_FALSE(lcd && pixels && pixel_count, ESP_ERR_INVALID_ARG,
                        TAG, "invalid frame chunk");

    const uint16_t command = lcd->frame_started ? NT35510_RAMWRC : NT35510_RAMWR;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(lcd->io, command, pixels,
                                                   pixel_count * sizeof(uint16_t)),
                        TAG, "frame chunk transfer failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(lcd->transfer_done, pdMS_TO_TICKS(1000)),
                        ESP_ERR_TIMEOUT, TAG, "frame chunk timeout");
    lcd->frame_started = true;
    return ESP_OK;
}

void nt35510_delete(nt35510_handle_t lcd)
{
    if (!lcd) {
        return;
    }
    if (lcd->transfer_done) {
        vSemaphoreDelete(lcd->transfer_done);
    }
    free(lcd);
}

/* Exported for use in esp_lcd_panel_io_i80_config_t. */
bool nt35510_color_transfer_done_callback(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *event_data,
                                          void *user_ctx)
{
    return color_transfer_done(panel_io, event_data, user_ctx);
}
