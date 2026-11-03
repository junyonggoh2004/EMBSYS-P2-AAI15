idf_component_register(
    SRCS
        "app_main.c"
        "bus_registry.c"
        "hcsr04.c"
        "tcrt5000.c"
        "tft_panel.c"
        "tsl2561.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc driver esp_timer
)
