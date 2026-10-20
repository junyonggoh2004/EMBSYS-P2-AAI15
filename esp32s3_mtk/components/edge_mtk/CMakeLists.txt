# ESP32-S3 platform layer on the course µT-Kernel 3 SMP port (D1 tasks 27-31):
# the HAL backend for the shared code, the USB console, and (later tasks) the
# µT-Kernel runtime, Wi-Fi/MQTT and model storage. Register-level drivers only
# (hal/*_ll.h), never esp_driver_* components, as in the course port.
idf_component_register(SRCS "console_mtk.c" "hal_mtk.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mtkernel edgecore esp_timer esp_hw_support esp_rom soc hal
                                esp_hal_gpio esp_hal_i2c esp_hal_ledc esp_hal_uart esp_hal_usb)

# Every printf/vprintf/puts/putchar in the image goes through console_mtk.c,
# so console output is serialised by a µT-Kernel mutex.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=printf" "-Wl,--wrap=vprintf"
                      "-Wl,--wrap=puts" "-Wl,--wrap=putchar")
