#include "bno085_hal_i2c.h"

#include <stdbool.h>
#include <string.h>

#include "bno085_board_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sh2_err.h"

static const char *TAG = "bno085_hal";

typedef struct {
    sh2_Hal_t sh2_hal;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
    volatile bool data_ready;
    bool transfer_pending;
    uint16_t pending_length;
    uint32_t pending_timestamp_us;
    bool gpio_ready;
    bool open;
    bno085_hal_stats_t stats;
} bno085_hal_context_t;

static bno085_hal_context_t s_hal;

static void IRAM_ATTR bno085_int_isr(void *arg)
{
    bno085_hal_context_t *ctx = (bno085_hal_context_t *)arg;
    ctx->data_ready = true;
}

static void record_i2c_error(esp_err_t err)
{
    s_hal.stats.i2c_error_count++;
    s_hal.stats.consecutive_error_count++;
    if (err == ESP_ERR_TIMEOUT) {
        s_hal.stats.timeout_count++;
    }
}

static esp_err_t configure_gpio_once(void)
{
    if (s_hal.gpio_ready) {
        return ESP_OK;
    }

    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << BNO085_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "reset GPIO configuration failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BNO085_RESET_GPIO, 0), TAG, "reset assertion failed");

    gpio_config_t int_config = {
        .pin_bit_mask = 1ULL << BNO085_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_config), TAG, "INT GPIO configuration failed");

    esp_err_t err = gpio_install_isr_service(0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BNO085_INT_GPIO, bno085_int_isr, &s_hal),
                        TAG, "INT handler installation failed");

    s_hal.gpio_ready = true;
    return ESP_OK;
}

static esp_err_t create_bus(void)
{
    if (s_hal.bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BNO085_I2C_PORT,
        .sda_io_num = BNO085_I2C_SDA_GPIO,
        .scl_io_num = BNO085_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_hal.bus),
                        TAG, "I2C bus creation failed");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BNO085_I2C_ADDRESS,
        .scl_speed_hz = BNO085_I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(s_hal.bus, &device_config, &s_hal.device);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_hal.bus);
        s_hal.bus = NULL;
        return err;
    }
    return ESP_OK;
}

static void destroy_bus(void)
{
    if (s_hal.device != NULL) {
        i2c_master_bus_rm_device(s_hal.device);
        s_hal.device = NULL;
    }
    if (s_hal.bus != NULL) {
        i2c_del_master_bus(s_hal.bus);
        s_hal.bus = NULL;
    }
}

static esp_err_t receive_with_retry(uint8_t *buffer, size_t length)
{
    esp_err_t err = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        err = i2c_master_receive(s_hal.device, buffer, length, BNO085_I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            s_hal.stats.consecutive_error_count = 0;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    record_i2c_error(err);
    ESP_LOGW(TAG, "I2C receive failed: length=%u error=%s",
             (unsigned)length, esp_err_to_name(err));
    return err;
}

static esp_err_t transmit_with_retry(const uint8_t *buffer, size_t length)
{
    esp_err_t err = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        err = i2c_master_transmit(s_hal.device, buffer, length, BNO085_I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            s_hal.stats.consecutive_error_count = 0;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    record_i2c_error(err);
    ESP_LOGW(TAG, "I2C transmit failed: length=%u error=%s",
             (unsigned)length, esp_err_to_name(err));
    return err;
}

static int hal_open(sh2_Hal_t *self)
{
    (void)self;
    if (s_hal.open) {
        return SH2_ERR;
    }
    if ((configure_gpio_once() != ESP_OK) || (create_bus() != ESP_OK)) {
        return SH2_ERR_IO;
    }

    s_hal.data_ready = false;
    s_hal.transfer_pending = false;
    s_hal.pending_length = 0;
    gpio_set_level(BNO085_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(BNO085_RESET_LOW_MS));
    gpio_set_level(BNO085_RESET_GPIO, 1);

    const int64_t deadline = esp_timer_get_time() + (BNO085_BOOT_WAIT_MS * 1000LL);
    while ((gpio_get_level(BNO085_INT_GPIO) != 0) && (esp_timer_get_time() < deadline)) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (gpio_get_level(BNO085_INT_GPIO) != 0) {
        s_hal.stats.timeout_count++;
        ESP_LOGE(TAG, "boot timeout: INT did not assert low");
        return SH2_ERR_IO;
    }

    esp_err_t probe_result = i2c_master_probe(s_hal.bus, BNO085_I2C_ADDRESS,
                                               BNO085_I2C_TIMEOUT_MS);
    if (probe_result != ESP_OK) {
        const uint8_t alternate_address =
            BNO085_I2C_ADDRESS == 0x4A ? 0x4B : 0x4A;
        esp_err_t alternate_result = i2c_master_probe(s_hal.bus, alternate_address,
                                                       BNO085_I2C_TIMEOUT_MS);
        if (alternate_result == ESP_OK) {
            ESP_LOGE(TAG,
                     "configured address 0x%02X did not ACK; BNO085 ACKed at 0x%02X. Change menuconfig",
                     BNO085_I2C_ADDRESS, alternate_address);
        } else {
            ESP_LOGE(TAG,
                     "BNO085 did not ACK at 0x4A or 0x4B; check power, I2C mode, SDA/SCL and pull-ups");
        }
        record_i2c_error(probe_result);
        return SH2_ERR_IO;
    }

    s_hal.data_ready = true;
    s_hal.open = true;
    ESP_LOGI(TAG, "I2C ready: address=0x%02X speed=%d SDA=%d SCL=%d INT=%d RESET=%d",
             BNO085_I2C_ADDRESS, BNO085_I2C_SPEED_HZ,
             BNO085_I2C_SDA_GPIO, BNO085_I2C_SCL_GPIO,
             BNO085_INT_GPIO, BNO085_RESET_GPIO);
    return SH2_OK;
}

static void hal_close(sh2_Hal_t *self)
{
    (void)self;
    gpio_set_level(BNO085_RESET_GPIO, 0);
    s_hal.data_ready = false;
    s_hal.transfer_pending = false;
    s_hal.pending_length = 0;
    s_hal.open = false;
}

static int hal_read(sh2_Hal_t *self, uint8_t *buffer, unsigned capacity, uint32_t *timestamp_us)
{
    (void)self;
    if ((buffer == NULL) || (timestamp_us == NULL) || (capacity < 4) || !s_hal.open) {
        return SH2_ERR_BAD_PARAM;
    }
    /* The CEVA SHTP assembler must receive the initial four-byte header as
     * one fragment. The BNO08x then presents the complete transfer with a
     * continuation header on the following I2C read. */
    if (s_hal.transfer_pending) {
        const uint16_t packet_length = s_hal.pending_length;
        if (packet_length > capacity) {
            s_hal.stats.invalid_packet_count++;
            s_hal.transfer_pending = false;
            return SH2_ERR_BAD_PARAM;
        }
        if (receive_with_retry(buffer, packet_length) != ESP_OK) {
            s_hal.stats.short_read_count++;
            return 0;
        }

        const uint16_t repeated_length = (uint16_t)buffer[0] |
                                         ((uint16_t)(buffer[1] & 0x7FU) << 8);
        if (repeated_length != packet_length) {
            s_hal.stats.invalid_packet_count++;
            s_hal.transfer_pending = false;
            ESP_LOGW(TAG, "SHTP header changed during read: %u -> %u",
                     packet_length, repeated_length);
            return 0;
        }

        s_hal.transfer_pending = false;
        s_hal.pending_length = 0;
        s_hal.data_ready = (gpio_get_level(BNO085_INT_GPIO) == 0);
        *timestamp_us = s_hal.pending_timestamp_us;
        return packet_length;
    }

    /* INTN is authoritative for the start of a new transfer. */
    if (gpio_get_level(BNO085_INT_GPIO) != 0) {
        s_hal.data_ready = false;
        return 0;
    }

    s_hal.data_ready = false;
    uint8_t header[4];
    if (receive_with_retry(header, sizeof(header)) != ESP_OK) {
        return 0;
    }

    const uint16_t packet_length = (uint16_t)header[0] |
                                   ((uint16_t)(header[1] & 0x7FU) << 8);
    if ((packet_length < 4) || (packet_length > capacity)) {
        s_hal.stats.invalid_packet_count++;
        ESP_LOGE(TAG, "invalid SHTP packet length: %u", packet_length);
        return 0;
    }

    memcpy(buffer, header, sizeof(header));
    s_hal.pending_length = packet_length;
    s_hal.pending_timestamp_us = (uint32_t)esp_timer_get_time();
    s_hal.transfer_pending = true;
    *timestamp_us = s_hal.pending_timestamp_us;
    return sizeof(header);
}

static int hal_write(sh2_Hal_t *self, uint8_t *buffer, unsigned length)
{
    (void)self;
    if ((buffer == NULL) || (length == 0) ||
        (length > SH2_HAL_MAX_TRANSFER_OUT) || !s_hal.open) {
        return SH2_ERR_BAD_PARAM;
    }
    return (transmit_with_retry(buffer, length) == ESP_OK) ? (int)length : SH2_ERR_IO;
}

static uint32_t hal_get_time_us(sh2_Hal_t *self)
{
    (void)self;
    return (uint32_t)esp_timer_get_time();
}

sh2_Hal_t *bno085_hal_i2c_get(void)
{
    s_hal.sh2_hal.open = hal_open;
    s_hal.sh2_hal.close = hal_close;
    s_hal.sh2_hal.read = hal_read;
    s_hal.sh2_hal.write = hal_write;
    s_hal.sh2_hal.getTimeUs = hal_get_time_us;
    return &s_hal.sh2_hal;
}

void bno085_hal_i2c_get_stats(bno085_hal_stats_t *out)
{
    if (out != NULL) {
        *out = s_hal.stats;
    }
}

esp_err_t bno085_hal_i2c_recreate_bus(void)
{
    if (s_hal.open) {
        return ESP_ERR_INVALID_STATE;
    }
    destroy_bus();
    return create_bus();
}
