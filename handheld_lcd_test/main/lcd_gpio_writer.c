#include "lcd_gpio_writer.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_struct.h"

#include "lcd_board_config.h"

#define LCD_DATA_MASK       0x0000FFFFU
#define LCD_WR_MASK         (1U << LCD_PIN_WR)

#define NT35510_CASET       0x2A00
#define NT35510_PASET       0x2B00
#define NT35510_RAMWR       0x2C00
#define NT35510_MADCTL      0x3600
#define NT35510_COLMOD      0x3A00
#define NT35510_DISPON      0x2900

static const char *TAG = "lcd_gpio";
static bool initialized;

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

static esp_err_t configure_data_bus(gpio_mode_t mode)
{
    const gpio_config_t config = {
        .pin_bit_mask = LCD_DATA_MASK,
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static inline void set_data_bus(uint16_t value)
{
    GPIO.out_w1tc = LCD_DATA_MASK;
    GPIO.out_w1ts = value;
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

static void read_reg_words(uint16_t reg, uint16_t *values, size_t count)
{
    write_command(reg);

    ESP_ERROR_CHECK(configure_data_bus(GPIO_MODE_INPUT));
    gpio_set_level(LCD_PIN_DC, 1);

    for (size_t i = 0; i < count; ++i) {
        /* FSMC also creates an independent CS pulse for every read access. */
        gpio_set_level(LCD_PIN_CS, 0);
        gpio_set_level(LCD_PIN_RD, 0);
        esp_rom_delay_us(10);
        values[i] = (uint16_t)(GPIO.in & LCD_DATA_MASK);
        gpio_set_level(LCD_PIN_RD, 1);
        gpio_set_level(LCD_PIN_CS, 1);
        esp_rom_delay_us(10);
    }

    ESP_ERROR_CHECK(configure_data_bus(GPIO_MODE_OUTPUT));
    set_data_bus(0);
}

static void read_reg_with_dummy(uint16_t reg, uint16_t *dummy,
                                uint16_t *value)
{
    uint16_t words[2];
    read_reg_words(reg, words, 2);
    *dummy = words[0];
    *value = words[1];
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
    * Sleep Out 이후 다시 설정해야 실제 패널에 유지됨.
    */
    write_reg(0xB500, 0x50);              // 480x800 gate 설정
    write_reg(NT35510_MADCTL, 0x60);      // 가로 800x480
    write_reg(NT35510_COLMOD, 0x66);      // 기존에 RGB가 정상인 RGB666 방식

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

static void set_window(uint16_t x_start, uint16_t y_start,
                       uint16_t x_end, uint16_t y_end)
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

static void log_window_registers(const char *label)
{
    uint16_t dummy[8];
    uint16_t value[8];
    const uint16_t regs[8] = {
        NT35510_CASET + 0, NT35510_CASET + 1,
        NT35510_CASET + 2, NT35510_CASET + 3,
        NT35510_PASET + 0, NT35510_PASET + 1,
        NT35510_PASET + 2, NT35510_PASET + 3,
    };

    for (size_t i = 0; i < 8; ++i) {
        read_reg_with_dummy(regs[i], &dummy[i], &value[i]);
    }
    ESP_LOGI(TAG,
             "%s window readback CASET=%04X/%04X %04X/%04X "
             "%04X/%04X %04X/%04X PASET=%04X/%04X %04X/%04X "
             "%04X/%04X %04X/%04X (dummy/value)",
             label,
             dummy[0], value[0], dummy[1], value[1],
             dummy[2], value[2], dummy[3], value[3],
             dummy[4], value[4], dummy[5], value[5],
             dummy[6], value[6], dummy[7], value[7]);
}


static void begin_full_frame(void)
{
    /*
     * 화면은 가로로 보이지만 LCD 주소 범위는
     * X = 0~479, Y = 0~799로 설정해야 전체가 채워짐.
     */
    set_window(
        0,
        0,
        LCD_V_RES - 1,  // 479
        LCD_H_RES - 1   // 799
    );

    write_command(NT35510_RAMWR);

    gpio_set_level(LCD_PIN_DC, 1);
    gpio_set_level(LCD_PIN_CS, 0);
}

static void end_frame(void)
{
    gpio_set_level(LCD_PIN_CS, 1);
}

esp_err_t lcd_gpio_writer_init(void)
{
    /* This direct path relies on LCD D0..D15 being GPIO0..GPIO15 in order. */
    ESP_RETURN_ON_FALSE(LCD_PIN_D0 == 0 && LCD_PIN_D1 == 1 &&
                        LCD_PIN_D2 == 2 && LCD_PIN_D3 == 3 &&
                        LCD_PIN_D4 == 4 && LCD_PIN_D5 == 5 &&
                        LCD_PIN_D6 == 6 && LCD_PIN_D7 == 7 &&
                        LCD_PIN_D8 == 8 && LCD_PIN_D9 == 9 &&
                        LCD_PIN_D10 == 10 && LCD_PIN_D11 == 11 &&
                        LCD_PIN_D12 == 12 && LCD_PIN_D13 == 13 &&
                        LCD_PIN_D14 == 14 && LCD_PIN_D15 == 15,
                        ESP_ERR_NOT_SUPPORTED, TAG,
                        "direct writer requires D0..D15 on GPIO0..GPIO15");

    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << LCD_PIN_WR) |
                        (1ULL << LCD_PIN_DC) |
                        (1ULL << LCD_PIN_CS) |
                        (1ULL << LCD_PIN_RD) |
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
    for (int pin = LCD_PIN_D0; pin <= LCD_PIN_D15; ++pin) {
        ESP_RETURN_ON_ERROR(
            gpio_set_drive_capability((gpio_num_t)pin, GPIO_DRIVE_CAP_0),
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
    gpio_set_level(LCD_PIN_RD, 1);
    set_data_bus(0);

    gpio_set_level(LCD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint16_t dummy_da;
    uint16_t dummy_db;
    uint16_t dummy_dc;
    uint16_t id1;
    uint16_t id2;
    uint16_t id3;
    uint16_t ddb[8];
    read_reg_with_dummy(0xDA00, &dummy_da, &id1);
    read_reg_with_dummy(0xDB00, &dummy_db, &id2);
    read_reg_with_dummy(0xDC00, &dummy_dc, &id3);
    read_reg_words(0xA100, ddb, sizeof(ddb) / sizeof(ddb[0]));
    ESP_LOGI(TAG,
             "LCD ID raw reads: DA=%04X/%04X DB=%04X/%04X DC=%04X/%04X "
             "(dummy/value)",
             dummy_da, id1, dummy_db, id2, dummy_dc, id3);
    ESP_LOGI(TAG,
             "LCD DDB A1 words: %04X %04X %04X %04X %04X %04X %04X %04X",
             ddb[0], ddb[1], ddb[2], ddb[3], ddb[4], ddb[5], ddb[6],
             ddb[7]);
    controller_init_nt35510();
    ESP_LOGI(TAG,
             "480x800 RGB666 packed stream: 3 transfers / 2 pixels");
    initialized = true;
    return ESP_OK;
}


void lcd_gpio_writer_fill(uint16_t color)
{
    if (!initialized) {
        return;
    }

    begin_full_frame();

    const uint8_t red = rgb565_red8(color);
    const uint8_t green = rgb565_green8(color);
    const uint8_t blue = rgb565_blue8(color);

    const uint16_t first =
        ((uint16_t)red << 8) | green;

    const uint16_t second =
        ((uint16_t)blue << 8) | red;

    const uint16_t third =
        ((uint16_t)green << 8) | blue;

    const size_t pixel_pair_count =
        ((size_t)LCD_H_RES * (size_t)LCD_V_RES) / 2;

    for (size_t i = 0; i < pixel_pair_count; ++i) {
        pulse_write(first);
        pulse_write(second);
        pulse_write(third);
    }

    end_frame();
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

    set_window(
        x,
        y,
        x + width - 1,
        y + height - 1
    );

    write_command(NT35510_RAMWR);

    gpio_set_level(LCD_PIN_DC, 1);
    gpio_set_level(LCD_PIN_CS, 0);

    const uint8_t red = rgb565_red8(color);
    const uint8_t green = rgb565_green8(color);
    const uint8_t blue = rgb565_blue8(color);

    const uint16_t first =
        ((uint16_t)red << 8) | green;

    const uint16_t second =
        ((uint16_t)blue << 8) | red;

    const uint16_t third =
        ((uint16_t)green << 8) | blue;

    const size_t pixel_count =
        (size_t)width * (size_t)height;

    for (size_t i = 0; i < pixel_count / 2; ++i) {
        pulse_write(first);
        pulse_write(second);
        pulse_write(third);
    }

    end_frame();
}

esp_err_t lcd_gpio_writer_draw(const uint16_t *pixels, size_t pixel_count)
{
    ESP_RETURN_ON_FALSE(initialized && pixels, ESP_ERR_INVALID_STATE, TAG,
                        "writer is not initialized");
    ESP_RETURN_ON_FALSE(pixel_count == (size_t)LCD_H_RES * LCD_V_RES,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "expected one complete 480x800 frame");

    begin_full_frame();
    for (size_t i = 0; i < pixel_count; i += 2) {
        const uint8_t r1 = rgb565_red8(pixels[i]);
        const uint8_t g1 = rgb565_green8(pixels[i]);
        const uint8_t b1 = rgb565_blue8(pixels[i]);
        const uint8_t r2 = rgb565_red8(pixels[i + 1]);
        const uint8_t g2 = rgb565_green8(pixels[i + 1]);
        const uint8_t b2 = rgb565_blue8(pixels[i + 1]);
        pulse_write(((uint16_t)r1 << 8) | g1);
        pulse_write(((uint16_t)b1 << 8) | r2);
        pulse_write(((uint16_t)g2 << 8) | b2);
    }
    end_frame();
    return ESP_OK;
}
