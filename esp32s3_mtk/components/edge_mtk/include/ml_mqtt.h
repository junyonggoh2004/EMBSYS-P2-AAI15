#pragma once
#include <stdbool.h>

/* SchedulerTask only. Results are QoS 1 and never silently truncated. */
bool mqtt_pub_ml_result(const char *json);
bool mqtt_pub_ml_status(bool ok, const char *reason, unsigned long generation);
