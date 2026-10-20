// services_stub_mtk.c
// Placeholders for the services later tasks port to µT-Kernel: Wi-Fi and MQTT
// (task 30) and model storage and TFLite Micro (task 31). Each answers with
// the documented "not available" result, so the whole shared core links and
// runs offline in the meantime.
#include <stdarg.h>
#include <stdio.h>
#include <tk/tkernel.h>

#include "hal/hal.h"
#include "inference_engine/tflite_wrapper.h"
#include "mqtt/mqtt_model_transfer.h"
#include "mqtt/mqtt_telemetry.h"
#include "runtime_mtk.h"

// ---------- MQTT (task 30) ----------

bool mqtt_init(const char *h, uint16_t p, const char *c, mqtt_cfg_block_cb_t a, mqtt_cmd_cb_t b, mqtt_node_msg_cb_t n)
{ (void)h; (void)p; (void)c; runtime_set_mqtt_handlers(a, b, n); return false; }
void mqtt_poll(void) {}
bool mqtt_is_connected(void) { return false; }
void mqtt_set_node_id(const char *id) { (void)id; }
bool mqtt_pub_text(const char *t, const char *fmt, ...) { (void)t; (void)fmt; return false; }
bool mqtt_pub_bytes(const char *s, const uint8_t *b, size_t l, int q, bool r) { (void)s; (void)b; (void)l; (void)q; (void)r; return false; }
bool mqtt_pub_line(const char *s, const char *l, int q, bool r) { (void)s; (void)l; (void)q; (void)r; return false; }
bool mqtt_pub_event(const char *r, const char *e, double v, const char *m, uint32_t ts) { (void)r; (void)e; (void)v; (void)m; (void)ts; return false; }
bool mqtt_pub_cfg_status(const char *n, bool ok) { (void)n; (void)ok; return false; }
bool mqtt_pub_rule_status(const char *n, bool ok) { (void)n; (void)ok; return false; }
void mqtt_publish_birth(bool r) { (void)r; }
void mqtt_publish_heartbeat(uint32_t s) { (void)s; }
bool mqtt_follow_remote_source(const char *o, const char *s, const char *a) { (void)o; (void)s; (void)a; return false; }

void mqtt_task_body(void)
{
    static pub_msg_t m;
    printf("[WIFI] not ported yet (task 30); running offline\n");
    for (;;) {
        tk_rcv_mbf(pub_mbf, &m, 1000);   // nothing to publish to
        runtime_alive(RT_MQTT);
    }
}
void mqtt_wifi_scan(void) { printf("[WIFI] not ported yet (task 30)\n"); }
void mqtt_wifi_drop(void) { printf("[WIFI] not ported yet (task 30)\n"); }

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
