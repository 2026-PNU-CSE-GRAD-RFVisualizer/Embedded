#include "lcd_gpio_writer.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"

#include "lcd_board_config.h"

#define LCD_DATA_LOW_MASK   0x0000FFFEU
#define LCD_D0_HIGH_MASK    (1U << (LCD_PIN_D0 - 32))
#define LCD_DATA_PIN_MASK   ((uint64_t)LCD_DATA_LOW_MASK | \
                             (1ULL << LCD_PIN_D0))
#define LCD_WR_MASK         (1U << LCD_PIN_WR)

#define NT35510_CASET       0x2A00
#define NT35510_PASET       0x2B00
#define NT35510_RAMWR       0x2C00
#define NT35510_MADCTL      0x3600
#define NT35510_COLMOD      0x3A00
#define NT35510_DISPON      0x2900

static const char *TAG = "lcd_gpio";
static bool initialized;
static esp_lcd_i80_bus_handle_t i80_bus;
static esp_lcd_panel_io_handle_t panel_io;
static SemaphoreHandle_t dma_done;
static uint16_t *dma_frame;

typedef struct {
    uint16_t red_green;
    uint16_t blue_red_mask;
    uint16_t green_blue;
} rgb332_lookup_t;

static rgb332_lookup_t rgb332_lookup[256];

#define LCD_FRAME_PIXELS ((size_t)LCD_H_RES * LCD_V_RES)
#define LCD_FRAME_WORDS  (LCD_FRAME_PIXELS * 3 / 2)
#define LCD_FRAME_BYTES  (LCD_FRAME_WORDS * sizeof(uint16_t))

/*
 * This panel revision keeps consuming three 8-bit colour components even
 * after a 0x55 COLMOD write. On a 16-bit 8080 bus, two RGB666 pixels are
 * transferred as three 16-bit words.
 */
static inline uint8_t rgb565_red8(uint16_t color)
{
    const uint8_t r5 = (color >> 11) & 0x1F;
    return (uint8_t)(((r5 << 3) | (r5 >> 2)) & 0xFC);
}

static inline uint8_t rgb565_green8(uint16_t color)
{
    const uint8_t g6 = (color >> 5) & 0x3F;
    return (uint8_t)(((g6 << 2) | (g6 >> 4)) & 0xFC);
}

static inline uint8_t rgb565_blue8(uint16_t color)
{
    const uint8_t b5 = color & 0x1F;
    return (uint8_t)(((b5 << 3) | (b5 >> 2)) & 0xFC);
}

static void prepare_rgb332_lookup(void)
{
    for (unsigned value = 0; value < 256; ++value) {
        const uint8_t red3 = (value >> 5) & 0x07;
        const uint8_t green3 = (value >> 2) & 0x07;
        const uint8_t blue2 = value & 0x03;
        const uint8_t red6 = (red3 << 3) | red3;
        const uint8_t green6 = (green3 << 3) | green3;
        const uint8_t blue6 = (blue2 << 4) | (blue2 << 2) | blue2;
        const uint8_t red8 = red6 << 2;
        const uint8_t green8 = green6 << 2;
        const uint8_t blue8 = blue6 << 2;

        rgb332_lookup[value].red_green =
            ((uint16_t)red8 << 8) | green8;
        rgb332_lookup[value].blue_red_mask =
            ((uint16_t)blue8 << 8) | red8;
        rgb332_lookup[value].green_blue =
            ((uint16_t)green8 << 8) | blue8;
    }
}

static esp_err_t configure_data_bus(gpio_mode_t mode)
{
    const gpio_config_t config = {
        .pin_bit_mask = LCD_DATA_PIN_MASK,
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static inline void set_data_bus(uint16_t value)
{
    GPIO.out_w1tc = LCD_DATA_LOW_MASK;
    GPIO.out_w1ts = (uint32_t)value & LCD_DATA_LOW_MASK;

    if ((value & 0x0001U) != 0) {
        GPIO.out1_w1ts.val = LCD_D0_HIGH_MASK;
    } else {
        GPIO.out1_w1tc.val = LCD_D0_HIGH_MASK;
    }
}

static inline void pulse_write(uint16_t value)
{
    set_data_bus(value);
    /* Conservative setup/hold times for a Dupont-wire breadboard test. */
    esp_rom_delay_us(2);
    GPIO.out_w1tc = LCD_WR_MASK;
    esp_rom_delay_us(2);
    GPIO.out_w1ts = LCD_WR_MASK;
    /* Keep data and D/C stable after the rising edge that latches the word. */
    esp_rom_delay_us(2);
}

static void write_command(uint16_t command)
{
    gpio_set_level(LCD_PIN_DC, 0);
    gpio_set_level(LCD_PIN_CS, 0);
    pulse_write(command);
    gpio_set_level(LCD_PIN_CS, 1);
}

static void write_reg(uint16_t reg, uint8_t value)
{
    /*
     * Match the Waveshare FSMC example exactly: its LCD_REG and LCD_RAM
     * stores are two independent bus accesses, so CS rises between the
     * command/address cycle and the data cycle.  Keeping CS asserted across
     * both cycles left this panel's extended registers at their reset values
     * (notably B500=0, the 480x640 gate setting).
     */
    write_command(reg);

    gpio_set_level(LCD_PIN_DC, 1);
    gpio_set_level(LCD_PIN_CS, 0);
    pulse_write((uint16_t)value);
    gpio_set_level(LCD_PIN_CS, 1);
}

static void write_regs(uint16_t first_reg, const uint8_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        write_reg(first_reg + i, values[i]);
    }
}

static void controller_init_nt35510(void)
{
    /* Values are from Waveshare's official 4inch Resistive Touch LCD demo. */
    static const uint8_t page1[] = {0x55, 0xAA, 0x52, 0x08, 0x01};
    static const uint8_t avdd_ratio[] = {0x34, 0x34, 0x34};
    static const uint8_t avdd[] = {0x0D, 0x0D, 0x0D};
    static const uint8_t avee_ratio[] = {0x24, 0x24, 0x24};
    static const uint8_t avee[] = {0x0D, 0x0D, 0x0D};
    static const uint8_t vcl_ratio[] = {0x24, 0x24, 0x24};
    static const uint8_t vgh_ratio[] = {0x24, 0x24, 0x24};
    static const uint8_t vgh[] = {0x05, 0x05, 0x05};
    static const uint8_t vgl_ratio[] = {0x34, 0x34, 0x34};
    static const uint8_t vgl[] = {0x0B, 0x0B, 0x0B};
    static const uint8_t vgmp[] = {0x00, 0xA3, 0x00};
    static const uint8_t vgmn[] = {0x00, 0xA3, 0x00};
    static const uint8_t vcom[] = {0x00, 0x63};
    static const uint8_t gamma[] = {
        0x00, 0x37, 0x00, 0x52, 0x00, 0x7B, 0x00, 0x99, 0x00, 0xB1,
        0x00, 0xD2, 0x00, 0xF6, 0x01, 0x27, 0x01, 0x4E, 0x01, 0x8C,
        0x01, 0xBE, 0x02, 0x0B, 0x02, 0x48, 0x02, 0x4A, 0x02, 0x7E,
        0x02, 0xBC, 0x02, 0xE1, 0x03, 0x10, 0x03, 0x31, 0x03, 0x5A,
        0x03, 0x73, 0x03, 0x94, 0x03, 0x9F, 0x03, 0xB3, 0x03, 0xB9,
        0x03, 0xC1,
    };
    static const uint8_t page0[] = {0x55, 0xAA, 0x52, 0x08, 0x00};
    static const uint8_t rgb_if[] = {0x08, 0x05, 0x02, 0x05, 0x02};
    static const uint8_t gate_eq[] = {0x00, 0x00};
    static const uint8_t source_eq[] = {0x01, 0x05, 0x05, 0x05};
    static const uint8_t inversion[] = {0x00, 0x00, 0x00};
    static const uint8_t boe[] = {0x03, 0x00, 0x00};
    static const uint8_t timing[] = {0x01, 0x84, 0x07, 0x31, 0x00};
    static const uint8_t command2[] = {0xAA, 0x55, 0x25, 0x01};

    write_regs(0xF000, page1, sizeof(page1));
    write_regs(0xB600, avdd_ratio, sizeof(avdd_ratio));
    write_regs(0xB000, avdd, sizeof(avdd));
    write_regs(0xB700, avee_ratio, sizeof(avee_ratio));
    write_regs(0xB100, avee, sizeof(avee));
    write_regs(0xB800, vcl_ratio, sizeof(vcl_ratio));
    write_reg(0xB200, 0x00);
    write_regs(0xB900, vgh_ratio, sizeof(vgh_ratio));
    write_regs(0xB300, vgh, sizeof(vgh));
    write_regs(0xBA00, vgl_ratio, sizeof(vgl_ratio));
    write_regs(0xB500, vgl, sizeof(vgl));
    write_regs(0xBC00, vgmp, sizeof(vgmp));
    write_regs(0xBD00, vgmn, sizeof(vgmn));
    write_regs(0xBE00, vcom, sizeof(vcom));

    write_regs(0xD100, gamma, sizeof(gamma));
    write_regs(0xD200, gamma, sizeof(gamma));
    write_regs(0xD300, gamma, sizeof(gamma));
    write_regs(0xD400, gamma, sizeof(gamma));
    write_regs(0xD500, gamma, sizeof(gamma));
    write_regs(0xD600, gamma, sizeof(gamma));

    write_regs(0xF000, page0, sizeof(page0));
    write_regs(0xB000, rgb_if, sizeof(rgb_if));
    write_reg(0xB600, 0x08);
    write_reg(0xB500, 0x50);
    write_regs(0xB700, gate_eq, sizeof(gate_eq));
    write_regs(0xB800, source_eq, sizeof(source_eq));
    write_regs(0xBC00, inversion, sizeof(inversion));
    write_regs(0xCC00, boe, sizeof(boe));
    write_regs(0xBD00, timing, sizeof(timing));
    write_reg(0xBA00, 0x01);
    write_regs(0xFF00, command2, sizeof(command2));
    // write_reg(0x3500, 0x00);
    // /* Landscape: swap row/column and scan left-to-right across 800 pixels. */
    // write_reg(NT35510_MADCTL, 0x60);
    // /* Explicitly select the 18-bit stream that this physical panel retains. */
    // write_reg(NT35510_COLMOD, 0x66);
    // write_command(0x1100);
    // vTaskDelay(pdMS_TO_TICKS(120));
    // write_command(NT35510_DISPON);
    // write_command(NT35510_RAMWR);

    write_reg(0x3500, 0x00);

    write_command(0x1100);  // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));

    /*
    * Sleep Out ?댄썑 ?ㅼ떆 ?ㅼ젙?댁빞 ?ㅼ젣 ?⑤꼸???좎???
    */
    write_reg(0xB500, 0x50);              // 480x800 gate ?ㅼ젙
    write_reg(NT35510_MADCTL, 0x60);      // 媛濡?800x480
    write_reg(NT35510_COLMOD, 0x66);      // 湲곗〈??RGB媛 ?뺤긽??RGB666 諛⑹떇

    write_command(0x1300);                // Normal Display Mode
    write_command(0x3800);                // Idle Mode Off
    vTaskDelay(pdMS_TO_TICKS(10));

    write_command(NT35510_DISPON);
    vTaskDelay(pdMS_TO_TICKS(50));
}

typedef struct {
    uint16_t reg;
    uint8_t value;
} lcd_reg_value_t;

static void __attribute__((unused)) controller_init_otm8009a(void)
{
    /*
     * 3.97-inch OTM8009A 16-bit-parallel initialization sequence. The
     * Waveshare panel specification now identifies this controller, while the
     * older Waveshare example targeted NT35510.
     */
    static const lcd_reg_value_t init[] = {
        {0xFF00, 0x80}, {0xFF01, 0x09}, {0xFF02, 0x01},
        {0xFF80, 0x80}, {0xFF81, 0x09}, {0xFF03, 0x01},
        {0xF5B6, 0x06}, {0xC480, 0x30}, {0xC48A, 0x40},
        {0xC0A3, 0x1B}, {0xC0BA, 0x50}, {0xC181, 0x66},
        {0xC1A1, 0x0E}, {0xC481, 0x83}, {0xC582, 0x83},
        {0xC590, 0x96}, {0xC591, 0x2B}, {0xC592, 0x01},
        {0xC594, 0x33}, {0xC595, 0x34}, {0xC5B1, 0xA9},
        {0xCE80, 0x86}, {0xCE81, 0x01}, {0xCE82, 0x00},
        {0xCE83, 0x85}, {0xCE84, 0x01}, {0xCE85, 0x00},
        {0xCE86, 0x00}, {0xCE87, 0x00}, {0xCE88, 0x00},
        {0xCE89, 0x00}, {0xCE8A, 0x00}, {0xCE8B, 0x00},
        {0xCEA0, 0x18}, {0xCEA1, 0x04}, {0xCEA2, 0x03},
        {0xCEA3, 0x21}, {0xCEA4, 0x00}, {0xCEA5, 0x00},
        {0xCEA6, 0x00}, {0xCEA7, 0x18}, {0xCEA8, 0x03},
        {0xCEA9, 0x03}, {0xCEAA, 0x22}, {0xCEAB, 0x00},
        {0xCEAC, 0x00}, {0xCEAD, 0x00}, {0xCEB0, 0x18},
        {0xCEB1, 0x02}, {0xCEB2, 0x03}, {0xCEB3, 0x23},
        {0xCEB4, 0x00}, {0xCEB5, 0x00}, {0xCEB6, 0x00},
        {0xCEB7, 0x18}, {0xCEB8, 0x01}, {0xCEB9, 0x03},
        {0xCEBA, 0x24}, {0xCEBB, 0x00}, {0xCEBC, 0x00},
        {0xCEBD, 0x00}, {0xCFC0, 0x01}, {0xCFC1, 0x01},
        {0xCFC2, 0x20}, {0xCFC3, 0x20}, {0xCFC4, 0x00},
        {0xCFC5, 0x00}, {0xCFC6, 0x01}, {0xCFC7, 0x00},
        {0xCFC8, 0x00}, {0xCFC9, 0x00}, {0xCFD0, 0x00},
        {0xCBC0, 0x00}, {0xCBC1, 0x04}, {0xCBC2, 0x04},
        {0xCBC3, 0x04}, {0xCBC4, 0x04}, {0xCBC5, 0x04},
        {0xCBC6, 0x00}, {0xCBC7, 0x00}, {0xCBC8, 0x00},
        {0xCBC9, 0x00}, {0xCBCA, 0x00}, {0xCBCB, 0x00},
        {0xCBCC, 0x00}, {0xCBCD, 0x00}, {0xCBCE, 0x00},
        {0xCBD0, 0x00}, {0xCBD1, 0x00}, {0xCBD2, 0x00},
        {0xCBD3, 0x00}, {0xCBD4, 0x00}, {0xCBD5, 0x00},
        {0xCBD6, 0x04}, {0xCBD7, 0x04}, {0xCBD8, 0x04},
        {0xCBD9, 0x04}, {0xCBDA, 0x04}, {0xCBDB, 0x00},
        {0xCBDC, 0x00}, {0xCBDD, 0x00}, {0xCBDE, 0x00},
        {0xCBE0, 0x00}, {0xCBE1, 0x00}, {0xCBE2, 0x00},
        {0xCBE3, 0x00}, {0xCBE4, 0x00}, {0xCBE5, 0x00},
        {0xCBE6, 0x00}, {0xCBE7, 0x00}, {0xCBE8, 0x00},
        {0xCBE9, 0x00}, {0xCC80, 0x00}, {0xCC81, 0x26},
        {0xCC82, 0x09}, {0xCC83, 0x0B}, {0xCC84, 0x01},
        {0xCC85, 0x25}, {0xCC86, 0x00}, {0xCC87, 0x00},
        {0xCC88, 0x00}, {0xCC89, 0x00}, {0xCC90, 0x00},
        {0xCC91, 0x00}, {0xCC92, 0x00}, {0xCC93, 0x00},
        {0xCC94, 0x00}, {0xCC95, 0x00}, {0xCC96, 0x00},
        {0xCC97, 0x00}, {0xCC98, 0x00}, {0xCC99, 0x00},
        {0xCC9A, 0x00}, {0xCC9B, 0x26}, {0xCC9C, 0x0A},
        {0xCC9D, 0x0C}, {0xCC9E, 0x02}, {0xCCA0, 0x25},
        {0xCCA1, 0x00}, {0xCCA2, 0x00}, {0xCCA3, 0x00},
        {0xCCA4, 0x00}, {0xCCA5, 0x00}, {0xCCA6, 0x00},
        {0xCCA7, 0x00}, {0xCCA8, 0x00}, {0xCCA9, 0x00},
        {0x3A00, 0x55}, {0xFF00, 0xFF}, {0xFF01, 0xFF},
        {0xFF02, 0xFF},
    };

    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        write_reg(init[i].reg, init[i].value);
    }

    write_reg(NT35510_MADCTL, 0x00);
    write_command(0x1100);
    vTaskDelay(pdMS_TO_TICKS(120));
    write_command(NT35510_DISPON);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void __attribute__((unused)) set_window(uint16_t x_start,
                                               uint16_t y_start,
                                               uint16_t x_end,
                                               uint16_t y_end)
{
    /* Match Waveshare's FSMC demo: every indexed byte is an independent
     * command access followed by an independent data access. */
    write_reg(NT35510_CASET + 0, x_start >> 8);
    write_reg(NT35510_CASET + 1, x_start & 0xFF);
    write_reg(NT35510_CASET + 2, x_end >> 8);
    write_reg(NT35510_CASET + 3, x_end & 0xFF);
    write_reg(NT35510_PASET + 0, y_start >> 8);
    write_reg(NT35510_PASET + 1, y_start & 0xFF);
    write_reg(NT35510_PASET + 2, y_end >> 8);
    write_reg(NT35510_PASET + 3, y_end & 0xFF);
}

static void __attribute__((unused)) begin_full_frame(void)
{
    /*
     * ?붾㈃? 媛濡쒕줈 蹂댁씠吏留?LCD 二쇱냼 踰붿쐞??     * X = 0~479, Y = 0~799濡??ㅼ젙?댁빞 ?꾩껜媛 梨꾩썙吏?
     */
    set_window(
        0,
        0,
        LCD_H_RES - 1,
        LCD_V_RES - 1
    );

    write_command(NT35510_RAMWR);

    gpio_set_level(LCD_PIN_DC, 1);
    gpio_set_level(LCD_PIN_CS, 0);
}

static void __attribute__((unused)) end_frame(void)
{
    gpio_set_level(LCD_PIN_CS, 1);
}

static bool dma_transfer_done(esp_lcd_panel_io_handle_t io,
                              esp_lcd_panel_io_event_data_t *event,
                              void *user_context)
{
    (void)io;
    (void)event;
    SemaphoreHandle_t done = (SemaphoreHandle_t)user_context;
    BaseType_t task_woken = pdFALSE;
    xSemaphoreGiveFromISR(done, &task_woken);
    return task_woken == pdTRUE;
}

static esp_err_t start_i80_dma(void)
{
    static const gpio_num_t data_pins[] = {
        LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
        LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        LCD_PIN_D8, LCD_PIN_D9, LCD_PIN_D10, LCD_PIN_D11,
        LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
    };

    for (size_t i = 0; i < sizeof(data_pins) / sizeof(data_pins[0]); ++i) {
        gpio_reset_pin(data_pins[i]);
    }
    gpio_reset_pin(LCD_PIN_WR);
    gpio_reset_pin(LCD_PIN_DC);
    gpio_reset_pin(LCD_PIN_CS);

    const esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
            LCD_PIN_D8, LCD_PIN_D9, LCD_PIN_D10, LCD_PIN_D11,
            LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
        },
        .bus_width = LCD_BUS_WIDTH,
        .max_transfer_bytes = LCD_FRAME_BYTES,
        .dma_burst_size = 32,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &i80_bus), TAG,
                        "failed to create I80 DMA bus");

    dma_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(dma_done != NULL, ESP_ERR_NO_MEM, TAG,
                        "failed to create DMA completion semaphore");

    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 1,
        .on_color_trans_done = dma_transfer_done,
        .user_ctx = dma_done,
        .dc_levels = {
            .dc_idle_level = 1,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 16,
        .flags = {
            .swap_color_bytes = LCD_SWAP_COLOR_BYTES,
        },
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i80(i80_bus, &io_config, &panel_io), TAG,
        "failed to create I80 panel IO");

    dma_frame = esp_lcd_i80_alloc_draw_buffer(
        panel_io, LCD_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(dma_frame != NULL, ESP_ERR_NO_MEM, TAG,
                        "failed to allocate %u-byte PSRAM DMA frame",
                        (unsigned)LCD_FRAME_BYTES);

    ESP_LOGI(TAG, "I80 DMA ready: %u Hz, one %u-byte PSRAM frame",
             LCD_PIXEL_CLOCK_HZ, (unsigned)LCD_FRAME_BYTES);
    return ESP_OK;
}

static esp_err_t __attribute__((unused)) dma_write_reg(uint16_t reg,
                                                       uint8_t value)
{
    const uint16_t word = value;
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(panel_io, reg, NULL, 0), TAG,
        "register command 0x%04X failed", reg);
    return esp_lcd_panel_io_tx_param(panel_io, -1, &word, sizeof(word));
}

static esp_err_t __attribute__((unused)) dma_set_window(
    uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_CASET + 0, x_start >> 8),
                        TAG, "CASET[0] failed");
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_CASET + 1, x_start & 0xFF),
                        TAG, "CASET[1] failed");
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_CASET + 2, x_end >> 8),
                        TAG, "CASET[2] failed");
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_CASET + 3, x_end & 0xFF),
                        TAG, "CASET[3] failed");
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_PASET + 0, y_start >> 8),
                        TAG, "PASET[0] failed");
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_PASET + 1, y_start & 0xFF),
                        TAG, "PASET[1] failed");
    ESP_RETURN_ON_ERROR(dma_write_reg(NT35510_PASET + 2, y_end >> 8),
                        TAG, "PASET[2] failed");
    return dma_write_reg(NT35510_PASET + 3, y_end & 0xFF);
}

static esp_err_t dma_begin_full_frame(void)
{
    /* The full-screen window is programmed once with the verified GPIO
     * register sequence before the pins are handed to the I80 peripheral.
     * RAMWR on the first DMA tile resets GRAM to that window's origin. */
    return ESP_OK;
}

esp_err_t lcd_gpio_writer_init(void)
{
    /* D1..D15 remain contiguous; D0 is written through GPIO bank 1. */
    ESP_RETURN_ON_FALSE(LCD_PIN_D0 == 38 && LCD_PIN_D1 == 1 &&
                        LCD_PIN_D2 == 2 && LCD_PIN_D3 == 3 &&
                        LCD_PIN_D4 == 4 && LCD_PIN_D5 == 5 &&
                        LCD_PIN_D6 == 6 && LCD_PIN_D7 == 7 &&
                        LCD_PIN_D8 == 8 && LCD_PIN_D9 == 9 &&
                        LCD_PIN_D10 == 10 && LCD_PIN_D11 == 11 &&
                        LCD_PIN_D12 == 12 && LCD_PIN_D13 == 13 &&
                        LCD_PIN_D14 == 14 && LCD_PIN_D15 == 15,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "direct writer pin map does not match board wiring");

    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << LCD_PIN_WR) |
                        (1ULL << LCD_PIN_DC) |
                        (1ULL << LCD_PIN_CS) |
                        (1ULL << LCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "GPIO config failed");
    ESP_RETURN_ON_ERROR(configure_data_bus(GPIO_MODE_OUTPUT), TAG,
                        "data GPIO config failed");

    /* Slow the very sharp ESP32-S3 edges to reduce ringing on Dupont wires. */
    static const gpio_num_t data_pins[] = {
        LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
        LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        LCD_PIN_D8, LCD_PIN_D9, LCD_PIN_D10, LCD_PIN_D11,
        LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
    };
    for (size_t i = 0; i < sizeof(data_pins) / sizeof(data_pins[0]); ++i) {
        ESP_RETURN_ON_ERROR(
            gpio_set_drive_capability(data_pins[i], GPIO_DRIVE_CAP_0),
            TAG, "data GPIO drive-strength config failed");
    }
    ESP_RETURN_ON_ERROR(
        gpio_set_drive_capability(LCD_PIN_WR, GPIO_DRIVE_CAP_0), TAG,
        "WR drive-strength config failed");
    ESP_RETURN_ON_ERROR(
        gpio_set_drive_capability(LCD_PIN_DC, GPIO_DRIVE_CAP_0), TAG,
        "DC drive-strength config failed");
    ESP_RETURN_ON_ERROR(
        gpio_set_drive_capability(LCD_PIN_CS, GPIO_DRIVE_CAP_0), TAG,
        "CS drive-strength config failed");

    gpio_set_level(LCD_PIN_CS, 1);
    gpio_set_level(LCD_PIN_DC, 1);
    gpio_set_level(LCD_PIN_WR, 1);
    set_data_bus(0);

    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "write-only LCD bus: D0=GPIO38, RD tied high");
    controller_init_nt35510();
    prepare_rgb332_lookup();
    /* MADCTL=0x60 presents the GRAM in landscape coordinates on this module.
     * Using the native portrait limits here updated only the left 480 pixels. */
    set_window(0, 0, LCD_H_RES - 1, LCD_V_RES - 1);
    ESP_LOGI(TAG, "GPIO-programmed full window: X=0..799, Y=0..479");
    ESP_RETURN_ON_ERROR(start_i80_dma(), TAG,
                        "failed to switch LCD frame path to DMA");
    ESP_LOGI(TAG,
             "landscape 800x480, RGB666 packed stream: "
             "3 transfers / 2 pixels");
    initialized = true;
    return ESP_OK;
}


static void pack_rgb666_pair(uint16_t first_color, uint16_t second_color,
                             uint16_t *output)
{
    const uint8_t r1 = rgb565_red8(first_color);
    const uint8_t g1 = rgb565_green8(first_color);
    const uint8_t b1 = rgb565_blue8(first_color);
    const uint8_t r2 = rgb565_red8(second_color);
    const uint8_t g2 = rgb565_green8(second_color);
    const uint8_t b2 = rgb565_blue8(second_color);

    output[0] = ((uint16_t)r1 << 8) | g1;
    output[1] = ((uint16_t)b1 << 8) | r2;
    output[2] = ((uint16_t)g2 << 8) | b2;
}

static esp_err_t dma_send_pixels(const uint16_t *pixels, uint16_t solid_color,
                                 bool solid, size_t pixel_count)
{
    ESP_RETURN_ON_FALSE(pixel_count == LCD_FRAME_PIXELS,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "DMA transfer requires one complete frame");

    for (size_t i = 0; i < pixel_count; i += 2) {
        const uint16_t first = solid ? solid_color : pixels[i];
        const uint16_t second = solid ? solid_color : pixels[i + 1];
        pack_rgb666_pair(first, second, &dma_frame[(i / 2) * 3]);
    }

    /* One command and one uninterrupted DMA transaction remove all tile/CS
     * boundaries from the 800x480 frame. */
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_color(panel_io, NT35510_RAMWR,
                                  dma_frame, LCD_FRAME_BYTES),
        TAG, "full-frame DMA submission failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(dma_done, portMAX_DELAY) == pdTRUE,
                        ESP_FAIL, TAG, "full-frame DMA completion failed");
    return ESP_OK;
}

void lcd_gpio_writer_fill(uint16_t color)
{
    if (!initialized) {
        return;
    }

    esp_err_t result = dma_begin_full_frame();
    if (result == ESP_OK) {
        result = dma_send_pixels(NULL, color, true,
                                 (size_t)LCD_H_RES * LCD_V_RES);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "DMA fill failed: %s", esp_err_to_name(result));
    }
}



void lcd_gpio_writer_fill_rect(uint16_t x, uint16_t y,
                               uint16_t width, uint16_t height,
                               uint16_t color)
{
    if (!initialized ||
        width == 0 ||
        height == 0 ||
        (uint32_t)x + width > LCD_H_RES ||
        (uint32_t)y + height > LCD_V_RES) {
        return;
    }

    if (x == 0 && y == 0 && width == LCD_H_RES && height == LCD_V_RES) {
        lcd_gpio_writer_fill(color);
        return;
    }
    ESP_LOGE(TAG, "partial rectangles are disabled on the fixed DMA window");
}

esp_err_t lcd_gpio_writer_draw(const uint16_t *pixels, size_t pixel_count)
{
    ESP_RETURN_ON_FALSE(initialized && pixels, ESP_ERR_INVALID_STATE, TAG,
                        "writer is not initialized");
    ESP_RETURN_ON_FALSE(pixel_count == (size_t)LCD_H_RES * LCD_V_RES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "expected one complete 800x480 landscape frame");

    ESP_RETURN_ON_ERROR(dma_begin_full_frame(), TAG,
                        "failed to begin DMA frame");
    return dma_send_pixels(pixels, 0, false, pixel_count);
}

esp_err_t lcd_gpio_writer_draw_rgb332(const uint8_t *pixels,
                                      size_t pixel_count)
{
    ESP_RETURN_ON_FALSE(initialized && pixels, ESP_ERR_INVALID_STATE, TAG,
                        "writer is not initialized");
    ESP_RETURN_ON_FALSE(pixel_count == LCD_FRAME_PIXELS,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "expected one complete 800x480 RGB332 frame");

    for (size_t i = 0; i < pixel_count; i += 2) {
        const rgb332_lookup_t *first = &rgb332_lookup[pixels[i]];
        const rgb332_lookup_t *second = &rgb332_lookup[pixels[i + 1]];
        uint16_t *output = &dma_frame[(i / 2) * 3];
        output[0] = first->red_green;
        output[1] = (first->blue_red_mask & 0xFF00) |
                    (second->blue_red_mask & 0x00FF);
        output[2] = second->green_blue;
    }

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_color(panel_io, NT35510_RAMWR,
                                  dma_frame, LCD_FRAME_BYTES),
        TAG, "RGB332 full-frame DMA submission failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(dma_done, portMAX_DELAY) == pdTRUE,
                        ESP_FAIL, TAG,
                        "RGB332 full-frame DMA completion failed");
    return ESP_OK;
}
