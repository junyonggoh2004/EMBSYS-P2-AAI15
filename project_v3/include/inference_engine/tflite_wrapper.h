#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- TFLite Micro Inference Wrapper ----------------
 *
 * This module exposes a pure-C API around the C++ TFLite Micro
 * interpreter.  The rest of the firmware (scheduler, rule engine)
 * calls only these functions — no C++ knowledge required.
 *
 * Lifecycle:
 *   1. model_transfer completes → model bytes are in Flash
 *   2. tflite_load_model()     → validates FlatBuffer, allocates tensors
 *   3. tflite_invoke()         → runs inference (can be called repeatedly)
 *   4. tflite_unload_model()   → frees interpreter (optional, before reload)
 */

/* Maximum supported tensor dimensions */
#define TFLITE_MAX_INPUTS   16
#define TFLITE_MAX_OUTPUTS  16

/* Arena size for the interpreter (tune as needed) */
#define TFLITE_ARENA_SIZE   (64 * 1024)   /* 64 KB */

/* Errors */
typedef enum {
    TFLITE_OK = 0,
    TFLITE_ERR_NO_MODEL,
    TFLITE_ERR_BAD_MODEL,
    TFLITE_ERR_ALLOC,
    TFLITE_ERR_INVOKE,
    TFLITE_ERR_NOT_LOADED,
} tflite_err_t;

/* Model metadata (populated after successful load) */
typedef struct {
    int     num_inputs;
    int     num_outputs;
    int     input_size;      /* total floats in input tensor(s)  */
    int     output_size;     /* total floats in output tensor(s) */
} tflite_model_info_t;

/* ---- API ---- */

/* Load and validate a model from a memory buffer (e.g. Flash XIP).
 * Returns TFLITE_OK on success. */
tflite_err_t tflite_load_model(const uint8_t *model_data, uint32_t model_size);

/* Run one inference.
 * Caller fills `inputs` with `info.input_size` floats.
 * On return, `outputs` contains `info.output_size` floats.
 * Returns TFLITE_OK on success. */
tflite_err_t tflite_invoke(const float *inputs, float *outputs);

/* Get metadata about the currently loaded model.
 * Returns NULL if no model is loaded. */
const tflite_model_info_t *tflite_get_info(void);

/* Unload the current model and free resources. */
void tflite_unload_model(void);

/* Check if a model is currently loaded and ready. */
bool tflite_is_ready(void);

#ifdef __cplusplus
}
#endif
