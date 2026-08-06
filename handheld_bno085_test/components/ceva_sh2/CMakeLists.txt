idf_component_register(
    SRCS
        "sh2.c"
        "sh2_SensorValue.c"
        "sh2_util.c"
        "shtp.c"
        "euler.c"
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
