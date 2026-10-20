# Minimal Wi-Fi reproduction for the course port (D1 task 30, MTK_REDESIGN.md
# section 10): the port's own wifi_sta component on its SMP profile, one idle
# task per core and one task that scans every 10 s and prints the station
# state. None of our code, no PSRAM. Build and flash like the firmware:
#   idf.py -C esp32s3_mtk/wifirepro -p <port> build flash monitor
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../third_party/mtk3smp-esp32s3/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifirepro)
