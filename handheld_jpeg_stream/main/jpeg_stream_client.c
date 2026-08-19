#include "jpeg_stream_client.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/netdb.h"

#include "jpeg_stream_protocol.h"

#define FRAME_BUFFER_COUNT 2

typedef struct {
    uint8_t *data;
    jpeg_stream_header_t header;
} frame_buffer_t;

typedef struct {
    char server_host[64];
    uint16_t server_port;
    size_t max_frame_bytes;
    uint32_t receive_timeout_ms;
    uint32_t reconnect_initial_ms;
    uint32_t reconnect_max_ms;
    jpeg_stream_frame_callback_t on_frame;
    void *user_context;
    frame_buffer_t buffers[FRAME_BUFFER_COUNT];
    QueueHandle_t free_buffers;
    QueueHandle_t ready_frames;
    jpeg_stream_client_stats_t stats;
    bool started;
} client_state_t;

static const char *TAG = "jpeg_stream";
static client_state_t s_client;

static bool is_jpeg(const uint8_t *data, size_t length)
{
    return length >= 4 && data[0] == 0xFF && data[1] == 0xD8 &&
           data[length - 2] == 0xFF && data[length - 1] == 0xD9;
}

static bool recv_exactly(int sock, uint8_t *destination, size_t length)
{
    size_t received = 0;
    while (received < length) {
        const ssize_t n = recv(sock, destination + received, length - received, 0);
        if (n == 0) {
            return false;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        received += (size_t)n;
    }
    return true;
}

static int connect_to_server(void)
{
    char port_text[6];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)s_client.server_port);

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *addresses = NULL;
    const int gai_result = getaddrinfo(s_client.server_host, port_text,
                                       &hints, &addresses);
    if (gai_result != 0 || addresses == NULL) {
        ESP_LOGW(TAG, "DNS/address lookup failed for %s:%s: %d",
                 s_client.server_host, port_text, gai_result);
        return -1;
    }

    int sock = -1;
    int last_socket_error = 0;
    for (struct addrinfo *address = addresses;
         address != NULL; address = address->ai_next) {
        sock = socket(address->ai_family, address->ai_socktype,
                      address->ai_protocol);
        if (sock < 0) {
            last_socket_error = errno;
            continue;
        }

        const struct timeval timeout = {
            .tv_sec = (time_t)(s_client.receive_timeout_ms / 1000),
            .tv_usec = (suseconds_t)((s_client.receive_timeout_ms % 1000) * 1000),
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        const int receive_buffer_bytes = 32 * 1024;
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes,
                   sizeof(receive_buffer_bytes));

        if (connect(sock, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        last_socket_error = errno;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(addresses);
    if (sock < 0) {
        ESP_LOGW(TAG, "TCP connect to %s:%s failed: errno=%d (%s)",
                 s_client.server_host, port_text, last_socket_error,
                 strerror(last_socket_error));
    }
    return sock;
}

static int acquire_receive_buffer(void)
{
    int index = -1;
    if (xQueueReceive(s_client.free_buffers, &index, 0) == pdTRUE) {
        return index;
    }

    if (xQueueReceive(s_client.ready_frames, &index, 0) == pdTRUE) {
        s_client.stats.stale_frames_dropped++;
        return index;
    }

    xQueueReceive(s_client.free_buffers, &index, portMAX_DELAY);
    return index;
}

static void release_buffer(int index)
{
    xQueueSend(s_client.free_buffers, &index, portMAX_DELAY);
}

static void publish_latest(int index)
{
    int stale_index = -1;
    if (xQueueReceive(s_client.ready_frames, &stale_index, 0) == pdTRUE) {
        s_client.stats.stale_frames_dropped++;
        release_buffer(stale_index);
    }
    xQueueSend(s_client.ready_frames, &index, portMAX_DELAY);
}

static bool receive_one_frame(int sock, uint32_t *last_seq, bool *have_seq)
{
    uint8_t raw_header[JPEG_STREAM_HEADER_SIZE];
    if (!recv_exactly(sock, raw_header, sizeof(raw_header))) {
        return false;
    }

    jpeg_stream_header_t header;
    const jpeg_stream_header_result_t result = jpeg_stream_parse_header(
        raw_header, s_client.max_frame_bytes, &header);
    if (result != JPEG_STREAM_HEADER_OK) {
        ESP_LOGW(TAG, "invalid frame header: %s",
                 jpeg_stream_header_result_name(result));
        s_client.stats.stream_errors++;
        return false;
    }

    const int index = acquire_receive_buffer();
    frame_buffer_t *buffer = &s_client.buffers[index];
    buffer->header = header;
    if (header.payload_length > 0 &&
        !recv_exactly(sock, buffer->data, header.payload_length)) {
        release_buffer(index);
        return false;
    }

    if (!is_jpeg(buffer->data, header.payload_length)) {
        ESP_LOGW(TAG, "seq=%lu is not a complete JPEG (%lu bytes)",
                 (unsigned long)header.seq,
                 (unsigned long)header.payload_length);
        s_client.stats.invalid_jpegs++;
        release_buffer(index);
        return true;
    }

    if (*have_seq) {
        const uint32_t delta = header.seq - *last_seq;
        if (delta > 1 && delta < 0x80000000u) {
            s_client.stats.sequence_gaps += delta - 1;
        }
    }
    *last_seq = header.seq;
    *have_seq = true;
    s_client.stats.frames_received++;
    publish_latest(index);
    return true;
}

static void receiver_task(void *argument)
{
    (void)argument;
    uint32_t backoff_ms = s_client.reconnect_initial_ms;

    for (;;) {
        ESP_LOGI(TAG, "connecting to %s:%u", s_client.server_host,
                 (unsigned)s_client.server_port);
        const int sock = connect_to_server();
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms = backoff_ms < s_client.reconnect_max_ms / 2
                ? backoff_ms * 2 : s_client.reconnect_max_ms;
            continue;
        }

        s_client.stats.reconnects++;
        backoff_ms = s_client.reconnect_initial_ms;
        ESP_LOGI(TAG, "connected to image relay viewer port");
        uint32_t last_seq = 0;
        bool have_seq = false;
        while (receive_one_frame(sock, &last_seq, &have_seq)) {
        }

        s_client.stats.stream_errors++;
        shutdown(sock, SHUT_RDWR);
        close(sock);
        ESP_LOGW(TAG, "stream disconnected; reconnecting in %lu ms",
                 (unsigned long)backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
    }
}

static void sink_task(void *argument)
{
    (void)argument;
    for (;;) {
        int index = -1;
        xQueueReceive(s_client.ready_frames, &index, portMAX_DELAY);
        frame_buffer_t *buffer = &s_client.buffers[index];
        const jpeg_stream_frame_t frame = {
            .seq = buffer->header.seq,
            .timestamp_ms = buffer->header.timestamp_ms,
            .flags = buffer->header.flags,
            .jpeg = buffer->data,
            .jpeg_length = buffer->header.payload_length,
        };
        s_client.on_frame(&frame, s_client.user_context);
        release_buffer(index);
    }
}

esp_err_t jpeg_stream_client_start(const jpeg_stream_client_config_t *config)
{
    if (config == NULL || config->server_host == NULL ||
        config->server_host[0] == '\0' || config->server_port == 0 ||
        config->max_frame_bytes == 0 || config->on_frame == NULL ||
        config->receive_timeout_ms == 0 || config->reconnect_initial_ms == 0 ||
        config->reconnect_max_ms < config->reconnect_initial_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client.started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(config->server_host) >= sizeof(s_client.server_host)) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&s_client, 0, sizeof(s_client));
    strcpy(s_client.server_host, config->server_host);
    s_client.server_port = config->server_port;
    s_client.max_frame_bytes = config->max_frame_bytes;
    s_client.receive_timeout_ms = config->receive_timeout_ms;
    s_client.reconnect_initial_ms = config->reconnect_initial_ms;
    s_client.reconnect_max_ms = config->reconnect_max_ms;
    s_client.on_frame = config->on_frame;
    s_client.user_context = config->user_context;

    s_client.free_buffers = xQueueCreate(FRAME_BUFFER_COUNT, sizeof(int));
    s_client.ready_frames = xQueueCreate(1, sizeof(int));
    if (s_client.free_buffers == NULL || s_client.ready_frames == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        s_client.buffers[i].data = heap_caps_malloc(
            s_client.max_frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_client.buffers[i].data == NULL) {
            ESP_LOGE(TAG, "PSRAM allocation failed for frame buffer %d", i);
            return ESP_ERR_NO_MEM;
        }
        xQueueSend(s_client.free_buffers, &i, portMAX_DELAY);
    }

    s_client.started = true;
    if (xTaskCreate(receiver_task, "jpeg_rx",
                    CONFIG_JPEG_STREAM_RX_TASK_STACK, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(sink_task, "jpeg_sink",
                    CONFIG_JPEG_STREAM_SINK_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void jpeg_stream_client_get_stats(jpeg_stream_client_stats_t *out)
{
    if (out != NULL) {
        *out = s_client.stats;
    }
}
