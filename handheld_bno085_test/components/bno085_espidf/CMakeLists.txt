idf_component_register(
    SRCS "bno085.c" "bno085_hal_i2c.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES ceva_sh2 esp_driver_gpio esp_driver_i2c esp_timer
)
