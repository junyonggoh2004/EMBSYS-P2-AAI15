#pragma once
#include <stdbool.h>

/* SchedulerTask only. Results are QoS 1 and never silently truncated. */
bool mqtt_pub_ml_result(const char *json);
bool mqtt_pub_ml_status(bool ok, const char *reason, unsigned long generation);
/* Live comfort result for the D6 panel. Other node roles ignore it. */
bool mqtt_pub_d6_comfort(unsigned score);
