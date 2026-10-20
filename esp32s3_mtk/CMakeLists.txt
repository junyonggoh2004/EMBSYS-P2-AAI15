# ESP32-S3 firmware on the course µT-Kernel 3 SMP port (D1 tasks 25-34).
#
# The course port (submodule third_party/mtk3smp-esp32s3, unmodified) supplies
# the kernel component, the FreeRTOS-API shim that IDF's own libraries use, and
# the shadowed freertos component. Our platform code is in components/ and the
# shared framework code is libraries/EdgeCore, compiled unchanged.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/third_party/mtk3smp-esp32s3/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_mtk)

# With PSRAM enabled, IDF's freertos/app_startup.c (compiled by the course
# port's shadow freertos component) includes esp_psram.h, but that component
# does not list esp_psram. Add the include path here instead of editing the
# course port. The code it guards is on FreeRTOS's start path, which the port
# bypasses.
idf_component_get_property(mtk_freertos_lib freertos COMPONENT_LIB)
idf_component_get_property(mtk_psram_dir esp_psram COMPONENT_DIR)
target_include_directories(${mtk_freertos_lib} PRIVATE ${mtk_psram_dir}/include)
