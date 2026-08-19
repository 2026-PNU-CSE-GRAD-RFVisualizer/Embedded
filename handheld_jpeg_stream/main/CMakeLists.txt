set(component_sources
        "app_main.c"
        "jpeg_lcd_sink.c"
        "jpeg_stream_client.c"
        "jpeg_stream_protocol.c"
        "lcd_gpio_writer.c"
)

if(CONFIG_HANDHELD_LOCAL_10FPS_TEST)
    list(APPEND component_sources "local_animation_test.c")
endif()

idf_component_register(
    SRCS
        ${component_sources}
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_event
        esp_driver_gpio
        esp_lcd
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
        esp_jpeg
)

if(CONFIG_HANDHELD_LOCAL_10FPS_TEST)
    set(animation_frames 00 01 02 03 04 05 06 07 08 09)
    foreach(frame_number IN LISTS animation_frames)
        target_add_binary_data(
            ${COMPONENT_LIB}
            "test_animation/frames/frame_${frame_number}.jpg"
            BINARY
        )
    endforeach()
endif()
