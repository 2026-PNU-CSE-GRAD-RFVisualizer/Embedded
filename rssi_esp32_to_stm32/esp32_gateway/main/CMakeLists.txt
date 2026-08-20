idf_component_register(
    SRCS
        "app_main.c"
        "espnow_receiver.c"
        "gateway_rssi.c"
        "uart_forwarder.c"
        "line_protocol.c"
        "gateway_time.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        lwip
        esp_netif
        nvs_flash
        esp_timer
        driver
)
