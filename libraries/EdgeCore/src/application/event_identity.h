#ifndef APPLICATION_EVENT_IDENTITY_H
#define APPLICATION_EVENT_IDENTITY_H
#include <stdbool.h>
#include <stdint.h>
/* Platform calls once, only after durably committing a new boot number.
 * Only SchedulerTask allocates sequences. CLEAR/APP RESET never reset this.
 * A full NVS erase requires reprovisioning identity before event production. */
bool event_identity_install(uint64_t node_id, uint32_t boot_id);
bool event_identity_ready(void);
bool event_identity_next(uint64_t *node_id, uint32_t *boot_id, uint32_t *sequence);
/* Reads this node's identity without taking a sequence number (D2-8 log records). */
bool event_identity_get(uint64_t *node_id, uint32_t *boot_id);
#endif
