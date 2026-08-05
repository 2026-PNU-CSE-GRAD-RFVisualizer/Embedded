idf_component_register(
    SRCS
        "app_main.c"
        "lcd_gpio_writer.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
)

target_add_binary_data(${COMPONENT_LIB} "testimage.rgb565" BINARY)
