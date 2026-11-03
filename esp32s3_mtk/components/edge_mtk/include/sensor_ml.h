#pragma once
#include <stdbool.h>
/* SchedulerTask owns configuration, samples and polling. InferenceTask owns tensors. */
bool sensor_ml_command(const char *line, bool sampling);
void sensor_ml_invalidate(void);
void sensor_ml_poll(bool sampling);
