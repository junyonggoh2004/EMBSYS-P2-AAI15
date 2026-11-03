# HAL test firmware for the µT-Kernel build (D1 task 27), driven by
# parity/hal_test.py. Same course port, same platform components and the same
# board settings as esp32s3_mtk; only main differs.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../third_party/mtk3smp-esp32s3/components
                         ${CMAKE_CURRENT_LIST_DIR}/../components)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_mtk_haltest)

# With PSRAM enabled, IDF's freertos/app_startup.c (compiled by the course
# port's shadow freertos component) includes esp_psram.h, but that component
# does not list esp_psram. Add the include path here instead of editing the
# course port. The code it guards is on FreeRTOS's start path, which the port
# bypasses.
idf_component_get_property(mtk_freertos_lib freertos COMPONENT_LIB)
idf_component_get_property(mtk_psram_dir esp_psram COMPONENT_DIR)
target_include_directories(${mtk_freertos_lib} PRIVATE ${mtk_psram_dir}/include)
