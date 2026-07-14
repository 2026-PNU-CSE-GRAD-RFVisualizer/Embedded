#include "rssi_filter.h"

#include <string.h>

void rssi_filter_init(rssi_filter_t *filter)
{
    memset(filter, 0, sizeof(*filter));
}

bool rssi_filter_add(rssi_filter_t *filter, int8_t raw_dbm)
{
    if (raw_dbm < RSSI_VALID_MIN_DBM || raw_dbm > RSSI_VALID_MAX_DBM) {
        return false;
    }

    filter->samples[filter->write_index] = raw_dbm;
    filter->write_index = (uint8_t)((filter->write_index + 1u) % RSSI_FILTER_WINDOW);
    if (filter->count < RSSI_FILTER_WINDOW) {
        filter->count++;
    }
    filter->latest_raw = raw_dbm;
    return true;
}

bool rssi_filter_get_average_x10(const rssi_filter_t *filter, int16_t *out_x10, uint8_t *out_count)
{
    if (filter->count == 0) {
        return false;
    }

    int32_t sum_x10 = 0;
    for (uint8_t i = 0; i < filter->count; ++i) {
        sum_x10 += (int32_t)filter->samples[i] * 10;
    }

    *out_x10 = (int16_t)(sum_x10 / filter->count);
    *out_count = filter->count;
    return true;
}
