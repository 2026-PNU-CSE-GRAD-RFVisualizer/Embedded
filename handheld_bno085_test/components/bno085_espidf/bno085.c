#include "bno085.h"

#include <math.h>
#include <string.h>

#include "bno085_board_config.h"
#include "bno085_hal_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sh2.h"
#include "sh2_SensorValue.h"
#include "sh2_err.h"

static const char *TAG = "bno085";

static sh2_Hal_t *s_hal;
static bool s_initialized;
static bool s_count_resets;
static bool s_initial_reset_seen;
static bool s_reset_pending;
static bool s_report_enabled;
static bool s_have_sequence;
static uint8_t s_last_sensor_sequence;
static uint32_t s_extended_sequence;
static uint64_t s_report_started_us;
static bno085_report_type_t s_report_type;
static uint32_t s_report_interval_us;
static bno085_quaternion_t s_latest;
static bno085_stats_t s_stats;

static esp_err_t sh2_result_to_esp(int result)
{
    switch (result) {
        case SH2_OK:
            return ESP_OK;
        case SH2_ERR_BAD_PARAM:
            return ESP_ERR_INVALID_ARG;
        case SH2_ERR_TIMEOUT:
            return ESP_ERR_TIMEOUT;
        default:
            return ESP_ERR_INVALID_RESPONSE;
    }
}

static void async_event_handler(void *cookie, sh2_AsyncEvent_t *event)
{
    (void)cookie;
    if (event->eventId == SH2_RESET) {
        s_initial_reset_seen = true;
        if (s_count_resets) {
            s_stats.unexpected_reset_count++;
            s_reset_pending = true;
            ESP_LOGW(TAG, "unexpected BNO085 reset event");
        } else {
            ESP_LOGI(TAG, "BNO085 reset advertisement received");
        }
    } else if (event->eventId == SH2_SHTP_EVENT) {
        ESP_LOGW(TAG, "SHTP event=%u", event->shtpEvent);
    }
}

static void sensor_event_handler(void *cookie, sh2_SensorEvent_t *event)
{
    (void)cookie;
    sh2_SensorValue_t value;
    if (sh2_decodeSensorEvent(&value, event) != SH2_OK) {
        s_stats.invalid_packet_count++;
        return;
    }
    if ((value.sensorId != SH2_GAME_ROTATION_VECTOR) &&
        (value.sensorId != SH2_ROTATION_VECTOR)) {
        return;
    }

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    if (s_stats.last_sample_us != 0) {
        uint64_t interval = now_us - s_stats.last_sample_us;
        uint32_t interval_us = interval > UINT32_MAX ? UINT32_MAX : (uint32_t)interval;
        if ((s_stats.min_interval_us == 0) || (interval_us < s_stats.min_interval_us)) {
            s_stats.min_interval_us = interval_us;
        }
        if (interval_us > s_stats.max_interval_us) {
            s_stats.max_interval_us = interval_us;
        }
    }

    if (s_have_sequence) {
        const uint8_t delta = (uint8_t)(value.sequence - s_last_sensor_sequence);
        if (delta > 1) {
            s_stats.sequence_loss_count += (uint32_t)(delta - 1);
        }
        s_extended_sequence += delta;
    } else {
        s_have_sequence = true;
        s_extended_sequence = 1;
    }
    s_last_sensor_sequence = value.sequence;

    if (value.sensorId == SH2_GAME_ROTATION_VECTOR) {
        s_latest.w = value.un.gameRotationVector.real;
        s_latest.x = value.un.gameRotationVector.i;
        s_latest.y = value.un.gameRotationVector.j;
        s_latest.z = value.un.gameRotationVector.k;
        s_latest.accuracy_rad = 0.0f;
    } else {
        s_latest.w = value.un.rotationVector.real;
        s_latest.x = value.un.rotationVector.i;
        s_latest.y = value.un.rotationVector.j;
        s_latest.z = value.un.rotationVector.k;
        s_latest.accuracy_rad = value.un.rotationVector.accuracy;
    }
    s_latest.accuracy_status = value.status & 0x03U;
    s_latest.timestamp_us = value.timestamp;
    s_latest.sequence = s_extended_sequence;

    const float norm = sqrtf((s_latest.w * s_latest.w) +
                             (s_latest.x * s_latest.x) +
                             (s_latest.y * s_latest.y) +
                             (s_latest.z * s_latest.z));
    if (!isfinite(norm)) {
        s_stats.non_finite_count++;
    } else if ((norm < 0.97f) || (norm > 1.03f)) {
        s_stats.norm_error_count++;
    }

    s_stats.sample_count++;
    s_stats.last_sample_us = now_us;
}

static esp_err_t log_product_ids(void)
{
    sh2_ProductIds_t ids;
    memset(&ids, 0, sizeof(ids));
    int result = sh2_getProdIds(&ids);
    if (result != SH2_OK) {
        ESP_LOGE(TAG, "Product ID query failed: %d", result);
        return sh2_result_to_esp(result);
    }
    if (ids.numEntries == 0) {
        ESP_LOGE(TAG, "Product ID response contained no entries");
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (uint8_t i = 0; i < ids.numEntries; ++i) {
        const sh2_ProductId_t *id = &ids.entry[i];
        ESP_LOGI(TAG, "part=%lu version=%u.%u.%u build=%lu reset_cause=%u",
                 (unsigned long)id->swPartNumber,
                 id->swVersionMajor, id->swVersionMinor, id->swVersionPatch,
                 (unsigned long)id->swBuildNumber, id->resetCause);
    }
    return ESP_OK;
}

static esp_err_t open_session(void)
{
    s_count_resets = false;
    s_initial_reset_seen = false;
    int result = sh2_open(s_hal, async_event_handler, NULL);
    if (result != SH2_OK) {
        ESP_LOGE(TAG, "sh2_open failed: %d", result);
        return sh2_result_to_esp(result);
    }
    if (!s_initial_reset_seen) {
        ESP_LOGE(TAG, "SH-2 opened without a reset advertisement");
        sh2_close();
        return ESP_ERR_TIMEOUT;
    }
    result = sh2_setSensorCallback(sensor_event_handler, NULL);
    if (result != SH2_OK) {
        sh2_close();
        return sh2_result_to_esp(result);
    }
    esp_err_t err = log_product_ids();
    if (err != ESP_OK) {
        sh2_close();
        return err;
    }
    s_count_resets = true;
    return ESP_OK;
}

esp_err_t bno085_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_latest, 0, sizeof(s_latest));
    memset(&s_stats, 0, sizeof(s_stats));
    s_reset_pending = false;
    s_report_enabled = false;
    s_have_sequence = false;
    s_hal = bno085_hal_i2c_get();

    esp_err_t err = open_session();
    if (err != ESP_OK) {
        return err;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t bno085_start_report(bno085_report_type_t report_type, uint32_t interval_us)
{
    if (!s_initialized || (interval_us == 0)) {
        return ESP_ERR_INVALID_STATE;
    }
    sh2_SensorId_t sensor_id;
    switch (report_type) {
        case BNO085_REPORT_GAME_ROTATION_VECTOR:
            sensor_id = SH2_GAME_ROTATION_VECTOR;
            break;
        case BNO085_REPORT_ROTATION_VECTOR:
            sensor_id = SH2_ROTATION_VECTOR;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    sh2_SensorConfig_t config = {
        .reportInterval_us = interval_us,
    };
    int result = sh2_setSensorConfig(sensor_id, &config);
    if (result != SH2_OK) {
        ESP_LOGE(TAG, "report enable failed: sensor=%u result=%d", sensor_id, result);
        return sh2_result_to_esp(result);
    }

    s_report_type = report_type;
    s_report_interval_us = interval_us;
    s_report_enabled = true;
    s_report_started_us = (uint64_t)esp_timer_get_time();
    ESP_LOGI(TAG, "report enabled: %s interval=%lu us",
             report_type == BNO085_REPORT_GAME_ROTATION_VECTOR ?
                 "GAME_ROTATION_VECTOR" : "ROTATION_VECTOR",
             (unsigned long)interval_us);
    return ESP_OK;
}

esp_err_t bno085_service(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    sh2_service();

    if (s_reset_pending) {
        s_reset_pending = false;
        if (s_report_enabled) {
            esp_err_t err = bno085_start_report(s_report_type, s_report_interval_us);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    bno085_hal_stats_t hal_stats;
    bno085_hal_i2c_get_stats(&hal_stats);
    if (hal_stats.consecutive_error_count >= BNO085_MAX_CONSECUTIVE_ERRORS) {
        return ESP_FAIL;
    }
    if (s_report_enabled && (s_stats.sample_count == 0) &&
        (((uint64_t)esp_timer_get_time() - s_report_started_us) > 1000000ULL)) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_report_enabled && (s_stats.last_sample_us != 0) &&
        (((uint64_t)esp_timer_get_time() - s_stats.last_sample_us) > 1000000ULL)) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

bool bno085_get_latest_quaternion(bno085_quaternion_t *out)
{
    if ((out == NULL) || (s_stats.sample_count == 0)) {
        return false;
    }
    *out = s_latest;
    return true;
}

void bno085_get_stats(bno085_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_stats;
    bno085_hal_stats_t hal_stats;
    bno085_hal_i2c_get_stats(&hal_stats);
    out->i2c_error_count = hal_stats.i2c_error_count;
    out->timeout_count += hal_stats.timeout_count;
    out->short_read_count = hal_stats.short_read_count;
    out->invalid_packet_count += hal_stats.invalid_packet_count;
}

esp_err_t bno085_recover(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const bool restart_report = s_report_enabled;
    const bno085_report_type_t report_type = s_report_type;
    const uint32_t interval_us = s_report_interval_us;
    s_stats.recovery_count++;
    s_count_resets = false;
    s_report_enabled = false;
    sh2_close();
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_err_t err = bno085_hal_i2c_recreate_bus();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus recreation failed: %s", esp_err_to_name(err));
        return err;
    }
    err = open_session();
    if (err != ESP_OK) {
        return err;
    }
    if (restart_report) {
        return bno085_start_report(report_type, interval_us);
    }
    return ESP_OK;
}

void bno085_deinit(void)
{
    if (s_initialized) {
        s_count_resets = false;
        sh2_close();
        s_initialized = false;
        s_report_enabled = false;
    }
}
