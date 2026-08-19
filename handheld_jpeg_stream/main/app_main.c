#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lcd_board_config.h"
#include "lcd_gpio_writer.h"
#include "jpeg_lcd_sink.h"
#include "jpeg_stream_client.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "handheld_jpeg";
static EventGroupHandle_t s_wifi_events;

static inline uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint16_t)(red & 0xF8) << 8) |
           ((uint16_t)(green & 0xFC) << 3) |
           ((uint16_t)blue >> 3);
}

static void show_boot_color_test(void)
{
    static const struct {
        const char *name;
        uint16_t color;
    } colors[] = {
        {"RED",   0xF800},
        {"GREEN", 0x07E0},
        {"BLUE",  0x001F},
    };

    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        ESP_LOGI(TAG, "boot LCD color test: %s", colors[i].name);
        lcd_gpio_writer_fill(colors[i].color);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    lcd_gpio_writer_fill(rgb565(0, 0, 0));
    ESP_LOGI(TAG, "boot LCD color test complete; waiting for server JPEG");
}

static void wifi_event_handler(void *argument, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)argument;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_HANDHELD_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_HANDHELD_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = strlen(CONFIG_HANDHELD_WIFI_PASSWORD) > 0
        ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void on_jpeg_frame(const jpeg_stream_frame_t *frame, void *user_context)
{
    (void)user_context;
    const esp_err_t result = jpeg_lcd_sink_render(frame);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "frame seq=%lu was not displayed: %s",
                 (unsigned long)frame->seq, esp_err_to_name(result));
    }
}

void app_main(void)
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    ESP_ERROR_CHECK(jpeg_lcd_sink_init());
    show_boot_color_test();

    if (CONFIG_HANDHELD_WIFI_SSID[0] == '\0' ||
        CONFIG_JPEG_STREAM_SERVER_HOST[0] == '\0') {
        ESP_LOGE(TAG, "set Wi-Fi and image relay host with: idf.py menuconfig");
        return;
    }

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(s_wifi_events == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    wifi_start();
    ESP_LOGI(TAG, "waiting for Wi-Fi");
    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        portMAX_DELAY);

    const jpeg_stream_client_config_t client_config = {
        .server_host = CONFIG_JPEG_STREAM_SERVER_HOST,
        .server_port = CONFIG_JPEG_STREAM_SERVER_PORT,
        .max_frame_bytes = CONFIG_JPEG_STREAM_MAX_FRAME_BYTES,
        .receive_timeout_ms = CONFIG_JPEG_STREAM_RX_TIMEOUT_MS,
        .reconnect_initial_ms = 1000,
        .reconnect_max_ms = 10000,
        .on_frame = on_jpeg_frame,
        .user_context = NULL,
    };
    ESP_ERROR_CHECK(jpeg_stream_client_start(&client_config));
    ESP_LOGI(TAG, "JPEG stream client started");
}
