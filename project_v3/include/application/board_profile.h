#ifndef APPLICATION_BOARD_PROFILE_H
#define APPLICATION_BOARD_PROFILE_H

#include <stdbool.h>
#include <stddef.h>

#include "bus/bus_common.h"

/*
 * Wiring profile for the Cytron Maker Pi Pico carrier used by this project.
 * The profile leaves the Grove signal pins available and protects pins that
 * are connected to the carrier's built-in peripherals.
 */
bool board_profile_validate_config(const app_cfg_t *cfg, char *reason, size_t reason_size);

#endif
