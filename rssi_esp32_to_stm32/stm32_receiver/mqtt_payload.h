#ifndef MQTT_PAYLOAD_H
#define MQTT_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "rssi_preprocessor.h"

#define MQTT_PAYLOAD_MAX_LEN    768

int mqtt_payload_build_snapshot(char *out,
                                size_t out_len,
                                const char *gateway_id,
                                const rssi_preprocessor_t *ctx,
                                uint32_t now_ms);

#endif
