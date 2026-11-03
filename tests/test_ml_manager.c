#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "inference_engine/inference_manager.h"
#include "inference_engine/tflite_wrapper.h"
#include "mqtt/mqtt_model_transfer.h"
static bool ready=true,failed;
static tflite_model_info_t info={.num_inputs=1,.num_outputs=1,.input_size=1,.output_size=1};
uint32_t hal_time_ms(void) { return 100; }
uint64_t hal_time_us(void) { return 100000; }
void hal_delay_ms(uint32_t n) { (void)n; }
tflite_err_t tflite_load_model(const uint8_t *p,uint32_t n) { ready=p && n; return ready ? TFLITE_OK:TFLITE_ERR_BAD_MODEL; }
bool tflite_is_ready(void) { return ready; }
const tflite_model_info_t *tflite_get_info(void) { return &info; }
tflite_err_t tflite_invoke(const float *p,float *o) { o[0]=p[0]*2; return failed ? TFLITE_ERR_INVOKE:TFLITE_OK; }
const uint8_t *model_transfer_get_model_ptr(void) { return NULL; }
uint32_t model_transfer_model_size(void) { return 0; }
model_status_t model_transfer_status(void) { return MODEL_IDLE; }
int main(void) {
    uint8_t model[]={1}; float x=0.5f,y=0;
    inference_mgr_init(); assert(inference_mgr_load_bytes(model,1));
    assert(!inference_mgr_get_output(0,&y));
    assert(!inference_mgr_set_inputs(&x,0)); assert(!inference_mgr_set_inputs(NULL,1));
    x=NAN; assert(!inference_mgr_set_inputs(&x,1)); x=0.5f;
    assert(inference_mgr_set_inputs(&x,1) && inference_mgr_run());
    assert(inference_mgr_get_output(0,&y) && y==1);
    failed=true; assert(!inference_mgr_run() && !inference_mgr_get_output(0,&y));
    failed=false; assert(inference_mgr_run());
    uint32_t rev=inference_mgr_revision();
    assert(inference_mgr_load_bytes(model,1) && inference_mgr_revision()!=rev && !inference_mgr_get_output(0,&y));
    assert(!inference_mgr_load_bytes(NULL,0));
    puts("ML manager lifecycle tests PASS (interpreter boundary simulated)");
}
