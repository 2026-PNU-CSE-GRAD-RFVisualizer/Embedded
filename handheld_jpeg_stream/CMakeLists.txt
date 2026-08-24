cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../handheld_bno085_test/components/bno085_espidf"
    "${CMAKE_CURRENT_LIST_DIR}/../handheld_bno085_test/components/ceva_sh2"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(handheld_jpeg_stream)
