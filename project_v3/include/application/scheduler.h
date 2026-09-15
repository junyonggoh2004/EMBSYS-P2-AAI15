// application/scheduler.h
#ifndef APPLICATION_SCHEDULER_H
#define APPLICATION_SCHEDULER_H

#include <stdbool.h>
#include "bus/bus_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------
// Multi-sensor scheduler public API
// ---------------------------------------------------------------------

/**
 * Reset all active sensors (clears slots and drops bus contexts).
 * Call this before loading a fresh set of configs.
 */
void scheduler_reset(void);

/**
 * Add one parsed app_cfg_t as an active sensor.
 * Returns slot index [0..MAX_ACTIVE-1] on success, <0 on error.
 */
int  scheduler_add_config(const app_cfg_t *cfg_in);

/**
 * Backward-compatibility: wipes and installs a single sensor (legacy path).
 * Equivalent to scheduler_reset(); scheduler_add_config(cfg);
 */
void scheduler_apply_config(const app_cfg_t *cfg);

/**
 * Run one tick for all active sensors.
 * - Honors poll vs stream and per-sensor freq_hz.
 * - Emits via publisher when data is available.
 * Pass running=true when RUN is active; false to idle/stop streams.
 */
void scheduler_run_all(bool running);

// ---------------------------------------------------------------------
// Compatibility shim for legacy single-sensor mode
// (only define this if app.h already declares scheduler_run_step)
// ---------------------------------------------------------------------
void scheduler_run_step(const app_cfg_t *cfg, bool running);


int  scheduler_count(void);
void scheduler_dump(void);

#ifdef __cplusplus
}
#endif

#endif // APPLICATION_SCHEDULER_H
