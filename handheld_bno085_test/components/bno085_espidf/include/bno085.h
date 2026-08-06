#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    BNO085_REPORT_GAME_ROTATION_VECTOR,
    BNO085_REPORT_ROTATION_VECTOR,
} bno085_report_type_t;

typedef struct {
    float w, x, y, z;
    float accuracy_rad;
    uint8_t accuracy_status;
    uint64_t timestamp_us;
    uint32_t sequence;
} bno085_quaternion_t;

typedef struct {
    uint32_t sample_count;
    uint32_t sequence_loss_count;
    uint32_t i2c_error_count;
    uint32_t timeout_count;
    uint32_t short_read_count;
    uint32_t invalid_packet_count;
    uint32_t unexpected_reset_count;
    uint32_t recovery_count;
    uint32_t norm_error_count;
    uint32_t non_finite_count;
    uint64_t last_sample_us;
    uint32_t min_interval_us;
    uint32_t max_interval_us;
} bno085_stats_t;

esp_err_t bno085_init(void);
esp_err_t bno085_start_report(bno085_report_type_t report_type, uint32_t interval_us);
esp_err_t bno085_service(void);
bool bno085_get_latest_quaternion(bno085_quaternion_t *out);
void bno085_get_stats(bno085_stats_t *out);
esp_err_t bno085_recover(void);
void bno085_deinit(void);
