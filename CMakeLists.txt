# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(project_v3 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# TFLite Micro library (from git submodule)
include(tflmicro.cmake)

# Add executable. Default name is the project name, version 0.1

add_executable(project_v3
    project_v3/main.c

    project_v3/application/repl.c
    project_v3/application/config_parser.c
    project_v3/application/scheduler.c
    project_v3/application/output_format.c

    project_v3/mqtt/mqtt_config_parser.c
    project_v3/mqtt/mqtt_json_parser.c
    project_v3/mqtt/mqtt_telemetry.c
    project_v3/mqtt/mqtt_model_transfer.c
    project_v3/mqtt/sensor_bridge.c

    project_v3/bus/i2c.c
    project_v3/bus/uart.c
    project_v3/bus/gpio.c
    project_v3/bus/bus_if.c

    project_v3/rule_engine/rules.c
    project_v3/rule_engine/rule_expr.c
    project_v3/rule_engine/rule_action.c

    project_v3/inference_engine/tflite_wrapper.cpp
    project_v3/inference_engine/inference_manager.c
    )

pico_set_program_name(project_v3 "project_v3")
pico_set_program_version(project_v3 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(project_v3 0)
pico_enable_stdio_usb(project_v3 1)

# Add library to the build
target_link_libraries(project_v3
    pico_stdlib
    hardware_i2c
    hardware_uart
    hardware_adc
    hardware_gpio
    hardware_timer
    hardware_pwm
    hardware_flash
    pico_lwip_mqtt
    pico_cyw43_arch_lwip_threadsafe_background
    pico_multicore
    jsmn
    pico-tflmicro
)

# Add the standard include files to the build
target_include_directories(project_v3 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/include
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/include/mqtt
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/application
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/mqtt
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/bus
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/rule_engine
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/inference_engine
)

add_library(jsmn STATIC
    project_v3/third_party/jsmn/jsmn.c
)

target_include_directories(jsmn PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR}/project_v3/include/third_party/jsmn
)

target_compile_definitions(jsmn PUBLIC
    JSMN_PARENT_LINKS=0
    JSMN_STRICT=0
)

pico_add_extra_outputs(project_v3)

# Standalone USB-serial monitor for a digital-output light sensor. This target
# deliberately has no Wi-Fi, MQTT, or TFLite dependency at runtime.
add_executable(digital_light_sensor
    project_v3/digital_light_sensor.c
)

target_link_libraries(digital_light_sensor
    pico_stdlib
    hardware_i2c
    hardware_gpio
    m
)

pico_set_program_name(digital_light_sensor "digital_light_sensor")
pico_set_program_version(digital_light_sensor "1.0")
pico_enable_stdio_uart(digital_light_sensor 0)
pico_enable_stdio_usb(digital_light_sensor 1)
pico_add_extra_outputs(digital_light_sensor)

# One USB-serial firmware image that reads all three requested sensors.
add_executable(multi_sensor_monitor
    project_v3/multi_sensor_monitor.c
)

target_link_libraries(multi_sensor_monitor
    pico_stdlib
    hardware_i2c
    hardware_gpio
    m
)

pico_set_program_name(multi_sensor_monitor "multi_sensor_monitor")
pico_set_program_version(multi_sensor_monitor "1.0")
pico_enable_stdio_uart(multi_sensor_monitor 0)
pico_enable_stdio_usb(multi_sensor_monitor 1)
pico_add_extra_outputs(multi_sensor_monitor)

