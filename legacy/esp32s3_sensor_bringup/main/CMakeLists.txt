idf_component_register(SRCS "main.c" "sensors.c" "platform_mtk.c" "usb_console.c"
    INCLUDE_DIRS "." REQUIRES mtkernel freertos_shim esp_rom esp_hw_support esp_hal_gpio esp_hal_usb)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=tm_snd_dat" "-Wl,--wrap=tm_rcv_dat" "-Wl,--wrap=tm_com_init")
