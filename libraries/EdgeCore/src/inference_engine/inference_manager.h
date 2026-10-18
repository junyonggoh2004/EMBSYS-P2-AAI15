#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Inference Manager ----
 *
 * Manages the lifecycle of loaded TFLite models and their outputs.
 * Provides a global registry of inference results that the RPN
 * rule engine can query via variable names like "model.0", "model.1".
 *
 * Workflow:
 *   1. Model arrives via MQTT → model_transfer stores it in Flash
 *   2. inference_mgr_load_from_flash() validates & loads it
 *   3. inference_mgr_set_inputs() feeds sensor-derived floats
 *   4. inference_mgr_run() executes inference
 *   5. Rule engine reads outputs via inference_mgr_get_output()
 */

#define INFER_MAX_OUTPUTS  16
#define INFER_MAX_INPUTS   16
#define INFER_MAX_SLOTS     4   /* number of sensor slots (groups of features) */
#define INFER_FEATURES_PER_SLOT 4
#define INFER_MAX_MASKS    16   /* max threshold masks */
#define INFER_MODEL_NAME   16

/* Initialise the inference manager. Call once at startup. */
void inference_mgr_init(void);

/* Attempt to load the pre-compiled base model (skips Flash).
 * Returns true if loaded successfully, false otherwise. */
bool inference_mgr_load_base_model(void);

/* Attempt to load the model currently in Flash.
 * Returns true if loaded successfully, false otherwise. */
bool inference_mgr_load_from_flash(void);

/* Set the input values for the next inference run.
 * `inputs` must point to `count` floats.
 * Returns true if accepted (count matches model expectation). */
bool inference_mgr_set_inputs(const float *inputs, int count);

/* Run one inference cycle. Returns true on success. */
bool inference_mgr_run(void);

/* Get a single output value by index (0-based).
 * Returns true and writes to *out if valid. */
bool inference_mgr_get_output(int index, float *out);

/* Get the number of output values from the loaded model.
 * Returns 0 if no model loaded. */
int inference_mgr_output_count(void);

/* Get the number of input values expected by the loaded model.
 * Returns 0 if no model loaded. */
int inference_mgr_input_count(void);

/* Check if a model is loaded and ready for inference. */
bool inference_mgr_is_ready(void);

/* Poll function — call periodically from main loop.
 * Handles auto-loading when model_transfer completes. */
void inference_mgr_poll(void);

/* ---- Option C: Binary Normalization Parameters ---- */

/* Receive binary normalization parameters from Python.
 * Format: N floats of means, N floats of stds,
 *         then M threshold entries (4-char mask + float value).
 * Called from MQTT model/params topic handler. */
void inference_mgr_set_params(const uint8_t *data, size_t len);

/* Set a single input feature by index.
 * The value will be automatically normalized using the stored mean/std.
 * The corresponding sensor slot is marked as "active" for mask calculation. */
void inference_mgr_set_input_slot(int feature_idx, float raw_value);

/* Run inference, compute MSE loss, and check against the threshold
 * for the current active sensor mask.
 * Returns true if anomaly detected (loss > threshold).
 * If `chain_action` is non-NULL, it points to an action string to fire on anomaly. */
bool inference_mgr_run_and_check(float *out_loss);

/* Get the last computed MSE loss value. */
float inference_mgr_get_loss(void);

/* Get the duration (µs) of the last tflite_invoke() call. */
int64_t inference_mgr_get_invoke_us(void);

/* Enable/disable per-cycle verbose tensor dump to stdout.
 * When enabled, prints all input and output tensor values each inference. */
void inference_mgr_set_verbose(bool enable);

/* Set the anomaly scoring mode:
 *   mode=0 → MSE  (autoencoder: reconstruction error between inputs and outputs)
 *   mode=1 → DIST (SVDD/encoder: L2 norm of the output latent vector)
 * Can also be set via MQTT cmd: "infer.mode mse" or "infer.mode distance" */
void inference_mgr_set_score_mode(int mode);

#ifdef __cplusplus
}
#endif
