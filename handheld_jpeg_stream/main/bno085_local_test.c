#include "bno085_local_test.h"

#include <inttypes.h>
#include <math.h>

#include "bno085.h"
#include "bno085_board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define IMU_TASK_STACK_BYTES 6144
#define IMU_TASK_PRIORITY 7
#define IMU_LOG_DIVIDER 50U
#define IMU_STATS_PERIOD_US 5000000ULL
#if CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST
#define ACTIVE_REPORT_INTERVAL_US CONFIG_BNO085_LCD_REPORT_INTERVAL_US
#else
#define ACTIVE_REPORT_INTERVAL_US CONFIG_BNO085_REPORT_INTERVAL_US
#endif

static const char *TAG = "bno085_lcd_test";
static SemaphoreHandle_t s_io_window_mutex;

static esp_err_t take_io_window(void)
{
    if (s_io_window_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(s_io_window_mutex, portMAX_DELAY) == pdTRUE
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

static void release_io_window(void)
{
    if (s_io_window_mutex != NULL) {
        xSemaphoreGive(s_io_window_mutex);
    }
}

static bno085_report_type_t selected_report(void)
{
#if CONFIG_BNO085_REPORT_ROTATION_VECTOR
    return BNO085_REPORT_ROTATION_VECTOR;
#else
    return BNO085_REPORT_GAME_ROTATION_VECTOR;
#endif
}

static const char *report_name(bno085_report_type_t report)
{
    return report == BNO085_REPORT_ROTATION_VECTOR
        ? "ROTATION_VECTOR"
        : "GAME_ROTATION_VECTOR";
}

static void log_statistics(uint64_t started_us)
{
    bno085_stats_t stats;
    bno085_get_stats(&stats);

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    const double elapsed_s = (double)(now_us - started_us) / 1000000.0;
    const double rate_hz = elapsed_s > 0.0
        ? (double)stats.sample_count / elapsed_s
        : 0.0;

    ESP_LOGI("bno085_stats",
             "samples=%" PRIu32 " rate=%.2fHz seq_loss=%" PRIu32
             " i2c_errors=%" PRIu32 " timeouts=%" PRIu32
             " short_reads=%" PRIu32 " invalid=%" PRIu32
             " resets=%" PRIu32 " recoveries=%" PRIu32
             " norm_errors=%" PRIu32 " non_finite=%" PRIu32
             " min_dt=%.2fms max_dt=%.2fms",
             stats.sample_count, rate_hz, stats.sequence_loss_count,
             stats.i2c_error_count, stats.timeout_count,
             stats.short_read_count, stats.invalid_packet_count,
             stats.unexpected_reset_count, stats.recovery_count,
             stats.norm_error_count, stats.non_finite_count,
             stats.min_interval_us / 1000.0,
             stats.max_interval_us / 1000.0);
}

static void imu_task(void *argument)
{
    (void)argument;
    const bno085_report_type_t report = selected_report();

    ESP_LOGI(TAG, "pins: SDA=%d SCL=%d INT=%d RESET=%d",
             BNO085_I2C_SDA_GPIO, BNO085_I2C_SCL_GPIO,
             BNO085_INT_GPIO, BNO085_RESET_GPIO);
    ESP_LOGI(TAG, "I2C: address=0x%02X speed=%d report=%s interval=%d us",
             BNO085_I2C_ADDRESS, BNO085_I2C_SPEED_HZ,
             report_name(report), ACTIVE_REPORT_INTERVAL_US);

    esp_err_t result = bno085_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "initialization failed: %s", esp_err_to_name(result));
        ESP_LOGE(TAG, "LCD test continues; check BNO085 power, mode, address, pull-ups, INT and RESET");
        vTaskDelete(NULL);
        return;
    }

    result = bno085_start_report(report, ACTIVE_REPORT_INTERVAL_US);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "report start failed: %s", esp_err_to_name(result));
        bno085_deinit();
        vTaskDelete(NULL);
        return;
    }

    const uint64_t started_us = (uint64_t)esp_timer_get_time();
    uint64_t next_stats_us = started_us + IMU_STATS_PERIOD_US;
    uint32_t last_sequence = 0;

    for (;;) {
        result = take_io_window();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "could not enter BNO I2C window: %s",
                     esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        result = bno085_service();
        release_io_window();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "service failed: %s; starting recovery",
                     esp_err_to_name(result));
            result = take_io_window();
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "could not enter recovery window: %s",
                         esp_err_to_name(result));
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            result = bno085_recover();
            release_io_window();
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "recovery failed: %s", esp_err_to_name(result));
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                ESP_LOGI(TAG, "recovery complete");
            }
            continue;
        }

        bno085_quaternion_t quaternion;
        if (bno085_get_latest_quaternion(&quaternion) &&
            quaternion.sequence != last_sequence) {
            last_sequence = quaternion.sequence;
            if ((quaternion.sequence % IMU_LOG_DIVIDER) == 0U) {
                const float norm = sqrtf(
                    quaternion.w * quaternion.w +
                    quaternion.x * quaternion.x +
                    quaternion.y * quaternion.y +
                    quaternion.z * quaternion.z);
                ESP_LOGI(TAG,
                         "%s seq=%" PRIu32
                         " q=[w=%.5f x=%.5f y=%.5f z=%.5f]"
                         " norm=%.5f accuracy=%u accuracy_rad=%.5f",
                         report_name(report), quaternion.sequence,
                         quaternion.w, quaternion.x,
                         quaternion.y, quaternion.z,
                         norm, quaternion.accuracy_status,
                         quaternion.accuracy_rad);
            }
        }

        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (now_us >= next_stats_us) {
            log_statistics(started_us);
            next_stats_us = now_us + IMU_STATS_PERIOD_US;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

esp_err_t bno085_local_test_start(void)
{
    s_io_window_mutex = xSemaphoreCreateMutex();
    if (s_io_window_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreate(
        imu_task, "bno085_imu", IMU_TASK_STACK_BYTES,
        NULL, IMU_TASK_PRIORITY, NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(s_io_window_mutex);
        s_io_window_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LCD/BNO I/O time-division enabled");
    return ESP_OK;
}

esp_err_t bno085_local_test_lcd_begin(void)
{
    return take_io_window();
}

void bno085_local_test_lcd_end(void)
{
    release_io_window();
}
