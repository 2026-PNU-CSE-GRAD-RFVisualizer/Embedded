idf_component_register(
    SRCS
        "app_main.c"
        "rssi_measure.c"
        "rssi_filter.c"
        "espnow_packet.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_timer
)
