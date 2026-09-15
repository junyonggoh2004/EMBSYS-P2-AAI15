#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Flash layout for model storage ----
 * Pico 2W has 4MB Flash. We reserve the last 256KB for the TFLite model.
 * Flash base (XIP):  0x10000000
 * Model region:      0x103C0000 .. 0x103FFFFF  (256 KB)
 *
 * IMPORTANT: flash_range_erase / flash_range_program use OFFSETS from
 *            the start of flash (0x10000000), not absolute addresses.
 */
#define MODEL_FLASH_SIZE     (256 * 1024)                    /* 256 KB          */
#define MODEL_FLASH_OFFSET   (4 * 1024 * 1024 - MODEL_FLASH_SIZE) /* offset from flash base */
#define MODEL_FLASH_ADDR     (0x10000000 + MODEL_FLASH_OFFSET)    /* XIP address     */

#define MODEL_CHUNK_MAX      1024    /* max bytes per MQTT chunk */

/* Transfer status */
typedef enum {
    MODEL_IDLE = 0,
    MODEL_RECEIVING,
    MODEL_READY,
    MODEL_ERROR
} model_status_t;

/* Initialise the model transfer subsystem.
 * Call once after mqtt_init(). Subscribes to model/* topics. */
void model_transfer_init(void);

/* Call from mqtt_incoming_data_cb when a model/* topic is detected. */
void model_transfer_on_data(const char *topic,
                            const uint8_t *data, size_t len);

/* Poll from the main loop (handles deferred flash writes). */
void model_transfer_poll(void);

/* Query current transfer state */
model_status_t model_transfer_status(void);

/* Total bytes of the model currently stored in flash (0 if none) */
uint32_t model_transfer_model_size(void);

/* Pointer to the model in XIP flash (NULL if not ready) */
const uint8_t *model_transfer_get_model_ptr(void);

#ifdef __cplusplus
}
#endif
