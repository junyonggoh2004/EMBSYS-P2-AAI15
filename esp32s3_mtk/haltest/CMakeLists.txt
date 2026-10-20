# HAL test firmware for the µT-Kernel build (D1 task 27), driven by
# parity/hal_test.py. Same course port, same platform components and the same
# board settings as esp32s3_mtk; only main differs.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../third_party/mtk3smp-esp32s3/components
                         ${CMAKE_CURRENT_LIST_DIR}/../components)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_mtk_haltest)
