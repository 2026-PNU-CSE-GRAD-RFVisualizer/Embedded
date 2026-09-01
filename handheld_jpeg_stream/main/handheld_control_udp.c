#include "handheld_control_udp.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "bno085.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"

#include "handheld_buttons.h"
#include "handheld_control_protocol.h"

#define CONTROL_TASK_STACK_BYTES 4096
#define CONTROL_TASK_PRIORITY 6
#define CONTROL_PERIOD_MS 20U
#define CONTROL_STATS_PERIOD_MS 5000U
#define CONTROL_RECONNECT_MS 1000U

typedef struct {
    char backend_host[64];
    uint16_t backend_port;
    uint32_t device_id;
    uint32_t session_id;
    uint32_t sent;
    uint32_t send_errors;
    uint32_t serialize_errors;
    uint32_t no_sample;
    bool started;
} control_state_t;

static const char *TAG = "handheld_control";
static control_state_t s_control;

static uint8_t button_flags(handheld_button_state_t buttons)
{
    uint8_t flags = 0;
    if (buttons.teleport_held) {
        flags |= HANDHELD_CONTROL_FLAG_TELEPORT_BUTTON_HELD;
    }
    if (buttons.height_cycle_held) {
        flags |= HANDHELD_CONTROL_FLAG_HEIGHT_CYCLE_BUTTON_HELD;
    }
    return flags;
}

static int connect_udp(void)
{
    char port_text[6];
    snprintf(port_text, sizeof(port_text), "%u",
             (unsigned)s_control.backend_port);

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *addresses = NULL;
    const int lookup_result = getaddrinfo(
        s_control.backend_host, port_text, &hints, &addresses);
    if (lookup_result != 0 || addresses == NULL) {
        ESP_LOGW(TAG, "address lookup failed for %s:%s: %d",
                 s_control.backend_host, port_text, lookup_result);
        return -1;
    }

    int sock = -1;
    int last_error = 0;
    for (struct addrinfo *address = addresses;
         address != NULL; address = address->ai_next) {
        sock = socket(address->ai_family, address->ai_socktype,
                      address->ai_protocol);
        if (sock < 0) {
            last_error = errno;
            continue;
        }
        if (connect(sock, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        last_error = errno;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(addresses);

    if (sock < 0) {
        ESP_LOGW(TAG, "UDP connect to %s:%s failed: errno=%d (%s)",
                 s_control.backend_host, port_text, last_error,
                 strerror(last_error));
    }
    return sock;
}

static void log_stats(uint32_t sample_seq)
{
    ESP_LOGI(TAG,
             "stats: sent=%" PRIu32 " send_errors=%" PRIu32
             " serialize_errors=%" PRIu32 " no_sample=%" PRIu32
             " sample_seq=%" PRIu32 " internal_free=%u largest=%u",
             s_control.sent, s_control.send_errors,
             s_control.serialize_errors, s_control.no_sample, sample_seq,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static void control_task(void *argument)
{
    (void)argument;
    int sock = -1;
    uint32_t sample_seq = 1;
    uint8_t last_button_flags = UINT8_MAX;
    TickType_t next_wake = xTaskGetTickCount();
    TickType_t next_stats = next_wake + pdMS_TO_TICKS(CONTROL_STATS_PERIOD_MS);

    ESP_LOGI(TAG,
             "RFHC v1 target=%s:%u device_id=%" PRIu32
             " session_id=0x%08" PRIX32 " rate=50Hz",
             s_control.backend_host, (unsigned)s_control.backend_port,
             s_control.device_id, s_control.session_id);
    ESP_LOGW(TAG,
             "initial integration uses identity q_mount; verify physical axes before final use");

    for (;;) {
        if (sock < 0) {
            sock = connect_udp();
            if (sock < 0) {
                vTaskDelay(pdMS_TO_TICKS(CONTROL_RECONNECT_MS));
                next_wake = xTaskGetTickCount();
                continue;
            }
            ESP_LOGI(TAG, "UDP path ready");
        }

        const handheld_button_state_t buttons = handheld_buttons_sample();
        const uint8_t current_button_flags = button_flags(buttons);
        if (current_button_flags != last_button_flags) {
            ESP_LOGI(TAG,
                     "buttons: teleport=%s height_cycle=%s RFHC_flags=0x%02X",
                     buttons.teleport_held ? "held" : "released",
                     buttons.height_cycle_held ? "held" : "released",
                     (unsigned)(HANDHELD_CONTROL_FLAG_ORIENTATION_VALID |
                                current_button_flags));
            last_button_flags = current_button_flags;
        }

        bno085_quaternion_t quaternion;
        if (!bno085_get_latest_quaternion(&quaternion)) {
            s_control.no_sample++;
        } else {
            const handheld_control_packet_t packet = {
                .flags = HANDHELD_CONTROL_FLAG_ORIENTATION_VALID |
                         current_button_flags,
                .device_id = s_control.device_id,
                .session_id = s_control.session_id,
                .sample_seq = sample_seq,
                .event_seq = 0,
                .timestamp_ms = 0,
                .quaternion_x = quaternion.x,
                .quaternion_y = quaternion.y,
                .quaternion_z = quaternion.z,
                .quaternion_w = quaternion.w,
            };
            uint8_t wire[HANDHELD_CONTROL_PACKET_SIZE];
            const handheld_control_serialize_result_t serialize_result =
                handheld_control_serialize(&packet, wire);
            if (serialize_result != HANDHELD_CONTROL_SERIALIZE_OK) {
                s_control.serialize_errors++;
                ESP_LOGW(TAG, "serialize failed: %s",
                         handheld_control_serialize_result_name(serialize_result));
            } else {
                const ssize_t written = send(sock, wire, sizeof(wire), 0);
                if (written == (ssize_t)sizeof(wire)) {
                    s_control.sent++;
                    sample_seq++;
                } else {
                    s_control.send_errors++;
                    const int send_errno = errno;
                    if (s_control.send_errors == 1 ||
                        (s_control.send_errors % 50U) == 0U) {
                        ESP_LOGW(TAG,
                                 "UDP send failed: written=%d errno=%d (%s), errors=%" PRIu32,
                                 (int)written, send_errno,
                                 strerror(send_errno), s_control.send_errors);
                    }
                    if (send_errno != ENOMEM && send_errno != ENOBUFS &&
                        send_errno != EAGAIN) {
                        close(sock);
                        sock = -1;
                    }
                }
            }
        }

        const TickType_t now = xTaskGetTickCount();
        if ((int32_t)(now - next_stats) >= 0) {
            log_stats(sample_seq);
            next_stats = now + pdMS_TO_TICKS(CONTROL_STATS_PERIOD_MS);
        }
        xTaskDelayUntil(&next_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

esp_err_t handheld_control_udp_start(
    const handheld_control_udp_config_t *config)
{
    if (config == NULL || config->backend_host == NULL ||
        config->backend_host[0] == '\0' || config->backend_port == 0 ||
        config->device_id == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_control.started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlcpy(s_control.backend_host, config->backend_host,
                sizeof(s_control.backend_host)) >= sizeof(s_control.backend_host)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_control.backend_port = config->backend_port;
    s_control.device_id = config->device_id;
    s_control.session_id = esp_random();
    if (s_control.session_id == 0) {
        s_control.session_id = 1;
    }

    const esp_err_t button_result = handheld_buttons_init();
    if (button_result != ESP_OK) {
        memset(&s_control, 0, sizeof(s_control));
        return button_result;
    }

    const BaseType_t created = xTaskCreate(
        control_task, "control_udp", CONTROL_TASK_STACK_BYTES,
        NULL, CONTROL_TASK_PRIORITY, NULL);
    if (created != pdPASS) {
        memset(&s_control, 0, sizeof(s_control));
        return ESP_ERR_NO_MEM;
    }
    s_control.started = true;
    return ESP_OK;
}
