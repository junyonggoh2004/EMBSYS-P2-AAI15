# ESP32-S3 platform layer on the course µT-Kernel 3 SMP port (D1 tasks 27-31):
# the HAL backend for the shared code, the USB console, the µT-Kernel runtime,
# Wi-Fi/MQTT, model storage and InferenceTask. Register-level drivers only
# (hal/*_ll.h), never esp_driver_* components, as in the course port.
idf_component_register(SRCS "console_mtk.c" "hal_mtk.c" "runtime_mtk.c" "net_mtk.c" "model_store_mtk.c" "inference_mtk.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES mtkernel edgecore esp_timer esp_hw_support esp_rom esp_system heap soc hal esp_hal_wdt
                                esp_hal_gpio esp_hal_i2c esp_hal_ledc esp_hal_uart esp_hal_usb
                                esp_wifi esp_netif esp_event nvs_flash lwip espressif__mqtt esp_hal_timg
                                esp_partition espressif__esp-tflite-micro
                       LDFRAGMENTS "port_iram.lf")

# Every printf/vprintf/puts/putchar in the image goes through console_mtk.c,
# so console output is serialised by a µT-Kernel mutex.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=printf" "-Wl,--wrap=vprintf"
                      "-Wl,--wrap=puts" "-Wl,--wrap=putchar")

# The shared header mqtt/mqtt_model_transfer.h has "/*" inside a comment;
# keep that warning non-fatal here as in the edgecore component.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=comment)
