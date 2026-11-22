#pragma once
#include <stdbool.h>
/* SchedulerTask owns configuration, samples and polling. InferenceTask owns tensors. */
bool sensor_ml_command(const char *line, bool sampling);
/* Provisional D4 Part 2 JSON contract on pico/<node>/cmd. */
bool sensor_ml_setup_json(const char *line, bool sampling);
void sensor_ml_invalidate(void);
void sensor_ml_poll(bool sampling);
