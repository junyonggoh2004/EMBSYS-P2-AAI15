#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "application/scheduler.h"
#include "application/sensor_measurement.h"
#include "inference_engine/inference_manager.h"
#include "inference_engine/tflite_wrapper.h"
static uint32_t now,rev;
static int runs,loads,dim=1,fail_loads;
static bool ready=true,fail_invoke;
static float seen[16];
const unsigned char g_autoencoder_model_data[]={1};
const int g_autoencoder_model_size=1;
static app_cfg_t configs[3];
uint32_t hal_time_ms(void) { return now; }
uint64_t hal_time_us(void) { return (uint64_t)now*1000; }
const app_cfg_t *scheduler_config_by_name(const char *name) {
    for(int i=0;i<3;i++) if(!strcmp(name,configs[i].name)) return &configs[i];
    return NULL;
}
bool inference_mgr_load_bytes(const uint8_t *bytes,uint32_t size) {
    (void)bytes; loads++; rev++;
    if(fail_loads) { fail_loads--; ready=false; return false; }
    ready=true; dim=size==16440 ? 16:1; return true;
}
uint32_t inference_mgr_revision(void) { return rev; }
bool inference_mgr_is_ready(void) { return ready; }
bool inference_mgr_set_inputs(const float *p,int n) { assert(n==dim); memcpy(seen,p,(size_t)n*sizeof(float)); return true; }
bool inference_mgr_run(void) { runs++; return !fail_invoke; }
int inference_mgr_output_count(void) { return dim; }
bool inference_mgr_get_output(int i,float *v) { assert(i<dim); *v=seen[i]; return true; }
/* Include the actual implementation, exposing private state only to assertions. */
#include "../esp32s3_mtk/components/edge_mtk/sensor_ml.c"
static const uint8_t aht[]={8,0x80,0,6,0,0};
static const uint8_t light[]={0,0,0,0,0,0,0,0};
static const uint8_t pulse[]={0xd0,7,1};
static void samples(void) {
    sensor_measurement_emit(&configs[0],aht,6);
    sensor_measurement_emit(&configs[1],light,8);
    sensor_measurement_emit(&configs[2],pulse,3);
}
static void cmd(const char *s) { assert(sensor_ml_command(s,false)); }
static void tick(void) { now+=interval_ms; sensor_ml_poll(true); }
int main(void) {
    strcpy(configs[0].name,"AHT"); configs[0].proto=proto_i2c; configs[0].i2c_sensor=I2C_SENSOR_AHT;
    strcpy(configs[1].name,"LIGHT"); configs[1].proto=proto_i2c; configs[1].i2c_sensor=I2C_SENSOR_TSL2561;
    strcpy(configs[2].name,"DISTANCE"); configs[2].proto=proto_gpio; configs[2].gpio_mode="pulse";
    assert(!sensor_ml_command("HELP",false));
    cmd("ML PRESET"); assert(active==0 && bound_count()==4 && !enabled);
    cmd("ML RUN"); assert(enabled); tick(); assert(runs==0);
    samples(); tick(); assert(runs==1);
    float expected=((35.0f/60)+(0.5f)+0+((34.3f-2)/398))*6.28318530718f/4;
    assert(fabsf(seen[0]-expected)<0.0001f); // includes valid zero lux
    sensor_measurement_notify(&configs[2],NULL,HAL_ERR_TIMEOUT); tick(); assert(runs==1);
    samples(); tick(); assert(runs==2);
    now+=6000; sensor_ml_poll(true); assert(runs==2); // stale
    samples(); now+=2600; sensor_measurement_notify(&configs[0],aht,6); sensor_ml_poll(true); assert(runs==2); // skew
    cmd("ML MODEL sine-int8"); assert(active==1 && !enabled); cmd("ML RUN"); samples(); tick(); assert(runs==3);
    rev++; tick(); assert(runs==3 && !enabled); // external model replacement
    cmd("ML PRESET"); cmd("ML RESET"); cmd("ML BIND 0 DISTANCE distance_cm 2 400"); cmd("ML RUN"); samples(); tick(); assert(runs==4);
    assert(fabsf(seen[0]-((34.3f-2)/398)*6.28318530718f)<0.0001f);
    sensor_ml_invalidate(); cmd("ML BIND 0 LIGHT light_lux 0 1000"); cmd("ML RUN"); samples(); tick(); assert(runs==5 && seen[0]==0); // cross-bus substitution
    uint32_t g=generation;
    cmd("ML BIND 0 AHT distance_cm 0 10"); assert(generation==g);
    cmd("ML BIND 0 unknown temperature_c 0 10"); assert(generation==g);
    cmd("ML BIND 0 AHT temperature_c nan 10"); assert(generation==g);
    cmd("ML BIND 0 AHT temperature_c 10 10"); assert(generation==g);
    cmd("ML TIMING 0 1000 100"); assert(generation==g);
    assert(sensor_ml_command("ML MODEL sine-float",true)); assert(active==0); // rejected while sampling
    cmd("ML PRESET autoencoder"); assert(active==2 && bound_count()==16);
    cmd("ML RUN"); samples(); tick(); assert(runs==6 && dim==16 && fabsf(seen[0]-35.0f/60)<0.0001f && seen[2]==0);
    cmd("ML UNBIND 7"); cmd("ML RUN"); assert(!enabled);
    cmd("ML PRESET"); fail_loads=1; cmd("ML MODEL sine-int8"); assert(!enabled && active==0 && ready);
    cmd("ML MODEL sine-float"); cmd("ML RUN"); samples(); fail_invoke=true; tick(); assert(runs==7);
    fail_invoke=false; tick(); assert(runs==8);
    sensor_ml_invalidate(); tick(); assert(runs==8);
    // wrap-safe sample ages, with fresh zero-millisecond samples allowed
    now=UINT32_MAX-100; cmd("ML PRESET"); cmd("ML RUN"); samples(); now=100; sensor_ml_poll(true); assert(runs==9);
    puts("sensor ML integration tests PASS (interpreter boundary simulated)");
}
