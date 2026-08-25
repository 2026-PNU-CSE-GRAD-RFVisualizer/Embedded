set(component_sources
        "app_main.c"
        "handheld_control_protocol.c"
        "handheld_control_udp.c"
        "jpeg_lcd_sink.c"
        "jpeg_stream_client.c"
        "jpeg_stream_protocol.c"
        "lcd_gpio_writer.c"
        "rgb332_zlib_sink.c"
)

set(main_component_requirements
        esp_event
        esp_driver_gpio
        esp_hw_support
        esp_lcd
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
        esp_jpeg
        esp_rom
)

if(CONFIG_HANDHELD_LOCAL_10FPS_TEST)
    if(CONFIG_HANDHELD_LOCAL_3D_TEST)
        list(APPEND component_sources "local_3d_test.c")
    else()
        list(APPEND component_sources "local_animation_test.c")
    endif()
endif()

if(CONFIG_HANDHELD_LOCAL_BNO085_LCD_TEST OR CONFIG_HANDHELD_CONTROL_UDP)
    list(APPEND component_sources "bno085_local_test.c")
endif()

idf_component_register(
    SRCS
        ${component_sources}
    INCLUDE_DIRS
        "."
    REQUIRES
        ${main_component_requirements}
    PRIV_REQUIRES
        bno085_espidf
)

if(CONFIG_HANDHELD_LOCAL_10FPS_TEST AND NOT CONFIG_HANDHELD_LOCAL_3D_TEST)
    set(animation_frames 00 01 02 03 04 05 06 07 08 09)
    foreach(frame_number IN LISTS animation_frames)
        target_add_binary_data(
            ${COMPONENT_LIB}
            "test_animation/frames/frame_${frame_number}.jpg"
            BINARY
        )
    endforeach()
endif()
