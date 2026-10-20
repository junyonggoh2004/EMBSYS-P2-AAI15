// services_stub_mtk.c
// Placeholders for the services task 31 ports to µT-Kernel: model storage and
// TFLite Micro. Each answers with the documented "not available" result, so
// the whole shared core links in the meantime.
#include <stdio.h>
#include <tk/tkernel.h>

#include "hal/hal.h"
#include "inference_engine/tflite_wrapper.h"
#include "mqtt/mqtt_model_transfer.h"
#include "runtime_mtk.h"

// ---------- model storage and inference (task 31) ----------

void model_transfer_init(void) { printf("[MODEL] model storage not ported yet (task 31)\n"); }
void model_transfer_on_data(const char *t, const uint8_t *d, size_t l) { (void)t; (void)d; (void)l; }
void model_transfer_poll(void) {}
model_status_t model_transfer_status(void) { return MODEL_IDLE; }
uint32_t model_transfer_model_size(void) { return 0; }
const uint8_t *model_transfer_get_model_ptr(void) { return NULL; }

tflite_err_t tflite_load_model(const uint8_t *d, uint32_t s) { (void)d; (void)s; return TFLITE_ERR_NO_MODEL; }
tflite_err_t tflite_invoke(const float *i, float *o) { (void)i; (void)o; return TFLITE_ERR_NOT_LOADED; }
const tflite_model_info_t *tflite_get_info(void) { return NULL; }
void tflite_unload_model(void) {}
bool tflite_is_ready(void) { return false; }

void inference_task_body(void)
{
    static uint8_t item[INFER_MSG_MAX];
    for (;;) {
        if (tk_rcv_mbf(infer_mbf, item, TMO_FEVR) > 0) runtime_alive(RT_INFERENCE);
    }
}
void inference_on_done(const sched_msg_t *m) { (void)m; }
