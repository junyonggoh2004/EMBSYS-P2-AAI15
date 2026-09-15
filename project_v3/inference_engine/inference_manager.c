/* inference_manager.c — Bridges model_transfer, tflite_wrapper, and the rule engine
 *
 * This module:
 *   - Auto-detects when a model transfer completes and loads it
 *   - Stores inference outputs in a flat array queryable by index
 *   - Provides set_inputs / run / get_output for the scheduler
 */

#include "inference_engine/inference_manager.h"
#include "inference_engine/tflite_wrapper.h"
#include "mqtt/mqtt_model_transfer.h"
#include <stdio.h>
#include <string.h>
#include "pico/time.h"

/* Pre-compiled test model */
#include "../models/model_data.h"
#include "../models/model_params.h"

/* ---- State ---- */
static bool s_auto_loaded = false;     /* have we auto-loaded the current flash model? */

/* Cached outputs from the most recent inference */
static float s_outputs[INFER_MAX_OUTPUTS];
static int   s_output_count = 0;

/* Cached inputs (set by caller before run) */
static float s_inputs[INFER_MAX_INPUTS];
static int   s_input_count = 0;

/* ---- Option C: Normalization Parameters ---- */
static float s_means[INFER_MAX_INPUTS];
static float s_stds[INFER_MAX_INPUTS];
static int   s_norm_count = 0;  /* number of features with norm params */

/* Mask-based thresholds */
typedef struct {
    char  mask[5];   /* e.g. "1111" + null */
    float threshold;
} mask_thresh_t;

static mask_thresh_t s_thresholds[INFER_MAX_MASKS];
static int s_thresh_count = 0;

/* Sensor slot tracking for mask generation */
static uint32_t s_slot_last_seen_ms[INFER_MAX_SLOTS];
static float s_last_loss = 0.0f;

/* Profiling/Debug state */
static int64_t s_last_invoke_us = 0;  /* duration of last tflite_invoke in µs */
static bool    s_verbose        = false; /* set true via MQTT cmd debug.infer */

/* Score mode: how to compute the anomaly loss from model outputs */
typedef enum { SCORE_MSE = 0, SCORE_DISTANCE = 1 } score_mode_t;
static score_mode_t s_score_mode = SCORE_MSE;  /* default: autoencoder MSE */

/* ---- Public API ---- */

void inference_mgr_init(void) {
    memset(s_outputs, 0, sizeof(s_outputs));
    memset(s_inputs,  0, sizeof(s_inputs));
    s_output_count = 0;
    s_input_count  = 0;
    s_auto_loaded  = false;
    s_norm_count   = 0;
    s_thresh_count = 0;
    s_last_loss    = 0.0f;
    memset(s_slot_last_seen_ms, 0, sizeof(s_slot_last_seen_ms));
    printf("[INFER] Inference manager initialised.\n");
}

bool inference_mgr_load_base_model(void) {
    if (g_autoencoder_model_size == 0) {
        printf("[INFER] Base model is empty.\n");
        return false;
    }

    tflite_err_t err = tflite_load_model(g_autoencoder_model_data, g_autoencoder_model_size);
    if (err != TFLITE_OK) {
        printf("[INFER] Failed to load base model: error %d\n", err);
        return false;
    }

    const tflite_model_info_t *info = tflite_get_info();
    if (info) {
        s_input_count  = info->input_size  < INFER_MAX_INPUTS  ? info->input_size  : INFER_MAX_INPUTS;
        s_output_count = info->output_size < INFER_MAX_OUTPUTS ? info->output_size : INFER_MAX_OUTPUTS;
    }

    printf("[INFER] Base model loaded: %d inputs, %d outputs\n",
           s_input_count, s_output_count);

    /* Load baked-in defaults for normalization and thresholds */
    s_norm_count = (s_input_count < BASE_MODEL_FEATURES) ? s_input_count : BASE_MODEL_FEATURES;
    for (int i = 0; i < s_norm_count; i++) {
        s_means[i] = g_base_model_means[i];
        s_stds[i]  = g_base_model_stds[i];
    }

    s_thresh_count = 0;
    for (int i = 0; i < BASE_MODEL_THRESHOLDS; i++) {
        strncpy(s_thresholds[i].mask, g_base_model_thresholds[i].mask, 5);
        s_thresholds[i].threshold = g_base_model_thresholds[i].threshold;
        s_thresh_count++;
    }
    printf("[INFER] Loaded %d base normalization params and %d threshold masks\n", 
           s_norm_count, s_thresh_count);

    return true;
}

bool inference_mgr_load_from_flash(void) {
    const uint8_t *model = model_transfer_get_model_ptr();
    uint32_t size = model_transfer_model_size();

    if (!model || size == 0) {
        printf("[INFER] No model in flash.\n");
        return false;
    }

    tflite_err_t err = tflite_load_model(model, size);
    if (err != TFLITE_OK) {
        printf("[INFER] Failed to load model: error %d\n", err);
        return false;
    }

    const tflite_model_info_t *info = tflite_get_info();
    if (info) {
        s_input_count  = info->input_size  < INFER_MAX_INPUTS  ? info->input_size  : INFER_MAX_INPUTS;
        s_output_count = info->output_size < INFER_MAX_OUTPUTS ? info->output_size : INFER_MAX_OUTPUTS;
    }

    printf("[INFER] Model loaded: %d inputs, %d outputs\n",
           s_input_count, s_output_count);
    return true;
}

bool inference_mgr_set_inputs(const float *inputs, int count) {
    if (!tflite_is_ready()) return false;

    int n = count < s_input_count ? count : s_input_count;
    memcpy(s_inputs, inputs, n * sizeof(float));

    /* Zero-pad if caller provided fewer inputs than expected */
    if (n < s_input_count) {
        memset(s_inputs + n, 0, (s_input_count - n) * sizeof(float));
    }

    return true;
}

bool inference_mgr_run(void) {
    if (!tflite_is_ready()) return false;

    tflite_err_t err = tflite_invoke(s_inputs, s_outputs);
    if (err != TFLITE_OK) {
        printf("[INFER] Invoke failed: error %d\n", err);
        return false;
    }
    return true;
}

bool inference_mgr_get_output(int index, float *out) {
    if (index < 0 || index >= s_output_count) return false;
    if (!tflite_is_ready()) return false;
    *out = s_outputs[index];
    return true;
}

int inference_mgr_output_count(void) {
    return s_output_count;
}

int inference_mgr_input_count(void) {
    return s_input_count;
}

bool inference_mgr_is_ready(void) {
    return tflite_is_ready();
}

void inference_mgr_poll(void) {
    /* Auto-load: when a model transfer completes, load it automatically */
    if (!s_auto_loaded && model_transfer_status() == MODEL_READY) {
        printf("[INFER] MODEL_READY detected — starting auto-load...\n");
        sleep_ms(20); /* wait for USB serial flush */
        if (inference_mgr_load_from_flash()) {
            printf("[INFER] Auto-load successful.\n");
            s_auto_loaded = true;
        } else {
            printf("[INFER] Auto-load FAILED.\n");
            s_auto_loaded = true; // don't retry until status changes
        }
    }

    /* Reset auto-load flag if a new transfer starts */
    if (model_transfer_status() == MODEL_RECEIVING) {
        s_auto_loaded = false;
    }
}

/* ---- Option C: Binary Parameters ---- */

void inference_mgr_set_params(const uint8_t *data, size_t len) {
    /*
     * Binary format from Python:
     *   [1 byte]  N = number of features
     *   [N * 4B]  means (float32 LE)
     *   [N * 4B]  stds  (float32 LE)
     *   [1 byte]  M = number of threshold entries
     *   [M * 8B]  each entry = 4-char mask + float32 LE threshold
     */
    if (!data || len < 1) {
        printf("[INFER] set_params: empty data\n");
        return;
    }

    size_t pos = 0;
    uint8_t N = data[pos++];
    if (N > INFER_MAX_INPUTS) N = INFER_MAX_INPUTS;

    /* Need N*4 means + N*4 stds */
    if (pos + N * 4 * 2 > len) {
        printf("[INFER] set_params: data too short for %d features\n", N);
        return;
    }

    /* Parse means */
    for (int i = 0; i < N; i++) {
        memcpy(&s_means[i], data + pos, 4);
        pos += 4;
    }
    /* Parse stds */
    for (int i = 0; i < N; i++) {
        memcpy(&s_stds[i], data + pos, 4);
        /* Guard against division by zero */
        if (s_stds[i] < 1e-6f) s_stds[i] = 1.0f;
        pos += 4;
    }
    s_norm_count = N;
    printf("[INFER] Loaded %d normalization params\n", N);

    /* Parse threshold entries */
    s_thresh_count = 0;
    if (pos < len) {
        uint8_t M = data[pos++];
        if (M > INFER_MAX_MASKS) M = INFER_MAX_MASKS;
        for (int i = 0; i < M && pos + 8 <= len; i++) {
            memcpy(s_thresholds[i].mask, data + pos, 4);
            s_thresholds[i].mask[4] = '\0';
            pos += 4;
            memcpy(&s_thresholds[i].threshold, data + pos, 4);
            pos += 4;
            s_thresh_count++;
        }
        printf("[INFER] Loaded %d threshold masks\n", s_thresh_count);
    }

    /* Parse optional trailing score_mode byte */
    if (pos < len) {
        uint8_t mode_byte = data[pos++];
        s_score_mode = (mode_byte == 1) ? SCORE_DISTANCE : SCORE_MSE;
        printf("[INFER] Score mode: %s\n",
               s_score_mode == SCORE_DISTANCE ? "DISTANCE (SVDD)" : "MSE (Autoencoder)");
    }
}

void inference_mgr_set_input_slot(int feature_idx, float raw_value) {
    if (feature_idx < 0 || feature_idx >= INFER_MAX_INPUTS) return;

    /* Auto-normalize if we have params */
    float normalized;
    if (feature_idx < s_norm_count) {
        normalized = (raw_value - s_means[feature_idx]) / s_stds[feature_idx];
    } else {
        normalized = raw_value; /* pass through if no norm data */
    }

    s_inputs[feature_idx] = normalized;

    /* Mark the corresponding sensor slot as active */
    int slot = feature_idx / INFER_FEATURES_PER_SLOT;
    if (slot < INFER_MAX_SLOTS) {
        s_slot_last_seen_ms[slot] = to_ms_since_boot(get_absolute_time());
    }
}

bool inference_mgr_run_and_check(float *out_loss) {
    if (!tflite_is_ready()) return false;

    /* Feed normalized inputs and run inference */
    inference_mgr_set_inputs(s_inputs, s_input_count > 0 ? s_input_count : s_norm_count);

    /* --- Timed invoke --- */
    absolute_time_t t_start = get_absolute_time();
    if (!inference_mgr_run()) return false;
    s_last_invoke_us = absolute_time_diff_us(t_start, get_absolute_time());

    /* --- Compute anomaly loss based on score mode --- */
    int n = s_norm_count > 0 ? s_norm_count : s_input_count;
    float loss = 0.0f;

    if (s_score_mode == SCORE_DISTANCE) {
        /* L2 norm of the output vector (SVDD/encoder: distance from origin in latent space) */
        for (int i = 0; i < s_output_count; i++) {
            float o = 0.0f;
            inference_mgr_get_output(i, &o);
            loss += o * o;
        }
        if (s_output_count > 0) loss /= (float)s_output_count;
    } else {
        /* MSE: mean squared reconstruction error (autoencoder) */
        for (int i = 0; i < n; i++) {
            float out_val = 0.0f;
            inference_mgr_get_output(i, &out_val);
            float diff = s_inputs[i] - out_val;
            loss += diff * diff;
        }
        if (n > 0) loss /= (float)n;
    }

    s_last_loss = loss;
    if (out_loss) *out_loss = loss;

    /* Build the active sensor mask string (e.g. "1010") */
    char mask[INFER_MAX_SLOTS + 1];
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < INFER_MAX_SLOTS; i++) {
        /* Slot expires and drops out of the mask if not updated in 5000ms */
        if (s_slot_last_seen_ms[i] != 0 && (now - s_slot_last_seen_ms[i]) < 5000) {
            mask[i] = '1';
        } else {
            mask[i] = '0';
        }
    }
    mask[INFER_MAX_SLOTS] = '\0';

    /* Look up threshold for this mask */
    float threshold = 999999.0f; /* default: never trigger if no match */
    for (int i = 0; i < s_thresh_count; i++) {
        if (strcmp(mask, s_thresholds[i].mask) == 0) {
            threshold = s_thresholds[i].threshold;
            break;
        }
    }

    printf("[INFER] invoke=%lldus | %s=%.4f mask=%s thresh=%.4f %s\n",
           (long long)s_last_invoke_us,
           s_score_mode == SCORE_DISTANCE ? "DIST" : "MSE",
           loss, mask, threshold,
           (loss > threshold) ? "ANOMALY!" : "OK");

    if (s_verbose) {
        int nn = s_norm_count > 0 ? s_norm_count : s_input_count;
        printf("[INFER VERBOSE] Inputs  :");
        for (int i = 0; i < nn; i++) printf(" %6.3f", s_inputs[i]);
        printf("\n[INFER VERBOSE] Outputs :");
        for (int i = 0; i < nn; i++) {
            float o = 0.0f; inference_mgr_get_output(i, &o);
            printf(" %6.3f", o);
        }
        printf("\n");
    }

    /* NOTE: Inputs and slot-active flags are intentionally NOT cleared here.
     * This allows multiple sensor rules to asynchronously accumulate their
     * values into the shared ML input slots (Sensor Fusion mode).
     * Slots persist until overwritten by the next infer.set call. */

    return (loss > threshold);
}

float inference_mgr_get_loss(void) {
    return s_last_loss;
}

int64_t inference_mgr_get_invoke_us(void) {
    return s_last_invoke_us;
}

void inference_mgr_set_verbose(bool enable) {
    s_verbose = enable;
    printf("[INFER] Verbose mode %s\n", enable ? "ON" : "OFF");
}

void inference_mgr_set_score_mode(int mode) {
    s_score_mode = (mode == 1) ? SCORE_DISTANCE : SCORE_MSE;
    printf("[INFER] Score mode set to: %s\n",
           s_score_mode == SCORE_DISTANCE ? "DISTANCE (SVDD)" : "MSE (Autoencoder)");
}
