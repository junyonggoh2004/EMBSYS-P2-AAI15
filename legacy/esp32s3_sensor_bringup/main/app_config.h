#pragma once
/* The Version 2 breadboard pin assignments. LCD is intentionally unused. */
#define SENSOR_SDA 1
#define SENSOR_SCL 2
#define ULTRASONIC_TRIG 4
#define ULTRASONIC_ECHO 5
#define SAMPLE_PERIOD_MS 2000
#define ECHO_TIMEOUT_US 30000
/* Physical CPU IDs are 0/1; this kernel names the processors 1/2. */
#define SENSOR_PROCESSOR TP_PRC1
#define LOGGER_PROCESSOR TP_PRC2
