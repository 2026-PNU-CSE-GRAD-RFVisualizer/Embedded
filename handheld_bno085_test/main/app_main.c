#include <inttypes.h>
#include <math.h>

#include "bno085.h"
#include "bno085_board_config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bno085_test";

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
    return report == BNO085_REPORT_ROTATION_VECTOR ?
        "ROTATION_VECTOR" : "GAME_ROTATION_VECTOR";
}

static void log_statistics(uint64_t started_us)
{
    bno085_stats_t stats;
    bno085_get_stats(&stats);
    const double elapsed_s = (double)((uint64_t)esp_timer_get_time() - started_us) / 1000000.0;
    const double rate_hz = elapsed_s > 0.0 ? (double)stats.sample_count / elapsed_s : 0.0;

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

void app_main(void)
{
    const bno085_report_type_t report = selected_report();
    ESP_LOGI(TAG, "handheld_bno085_test start");
    ESP_LOGI(TAG, "pins: SDA=%d SCL=%d INT=%d RESET=%d",
             BNO085_I2C_SDA_GPIO, BNO085_I2C_SCL_GPIO,
             BNO085_INT_GPIO, BNO085_RESET_GPIO);
    ESP_LOGI(TAG, "I2C: address=0x%02X speed=%d report=%s interval=%d us",
             BNO085_I2C_ADDRESS, BNO085_I2C_SPEED_HZ,
             report_name(report), BNO085_REPORT_US);

    esp_err_t err = bno085_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BNO085 initialization failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "check power, common GND, I2C mode, address, pull-ups, INT and RESET");
        return;
    }
    err = bno085_start_report(report, BNO085_REPORT_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "report start failed: %s", esp_err_to_name(err));
        return;
    }

    const uint64_t started_us = (uint64_t)esp_timer_get_time();
    uint64_t next_stats_us = started_us + 5000000ULL;
    uint32_t last_sequence = 0;

    while (true) {
        err = bno085_service();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "sensor service failed: %s; starting recovery",
                     esp_err_to_name(err));
            err = bno085_recover();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "recovery failed: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(500));
            } else {
                ESP_LOGI(TAG, "recovery complete");
            }
            continue;
        }

        bno085_quaternion_t q;
        if (bno085_get_latest_quaternion(&q) && (q.sequence != last_sequence)) {
            last_sequence = q.sequence;
            if ((q.sequence % BNO085_LOG_DIVIDER) == 0) {
                const float norm = sqrtf((q.w * q.w) + (q.x * q.x) +
                                         (q.y * q.y) + (q.z * q.z));
                ESP_LOGI(TAG,
                         "%s seq=%" PRIu32
                         " q=[w=%.5f x=%.5f y=%.5f z=%.5f]"
                         " norm=%.5f accuracy=%u accuracy_rad=%.5f",
                         report_name(report), q.sequence,
                         q.w, q.x, q.y, q.z, norm,
                         q.accuracy_status, q.accuracy_rad);
            }
        }

        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (now_us >= next_stats_us) {
            log_statistics(started_us);
            next_stats_us = now_us + 5000000ULL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
