#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "app_config.h"
#include "platform.h"
#include "sensors.h"
#if !CONFIG_MTK_SMP
#error "Dual-core micro T-Kernel is required."
#endif
static ID snapshot_mutex;
static sensor_sample_t latest;

static void idle_task(INT code,void *context) {
    (void)code; (void)context;
    for (;;) __asm__ volatile("waiti 0" ::: "memory");
}
static void sensor_task(INT code,void *context) {
    (void)code; (void)context;
    platform_init(); platform_sleep_ms(200);
    uint32_t sequence=0;
    for (;;) {
        sensor_sample_t sample; sensors_read(&sample);
        sample.sequence=++sequence; sample.acquisition_processor=tk_get_prc();
        if (tk_loc_mtx(snapshot_mutex,100)==E_OK) {
            latest=sample; tk_unl_mtx(snapshot_mutex);
        }
        platform_sleep_ms(SAMPLE_PERIOD_MS);
    }
}
static void logger_task(INT code,void *context) {
    (void)code; (void)context;
    uint32_t last_sequence=0;
    for (;;) {
        sensor_sample_t sample;
        if (tk_loc_mtx(snapshot_mutex,100)!=E_OK) { tk_dly_tsk(50); continue; }
        sample=latest; tk_unl_mtx(snapshot_mutex);
        if (sample.sequence && sample.sequence!=last_sequence) {
            char distance[24]="null",temperature[24]="null",humidity[24]="null",light[24]="null";
            char line[768];
            if (sample.distance_valid) snprintf(distance,sizeof(distance),"%.1f",(double)sample.distance_cm);
            if (sample.aht_valid) {
                snprintf(temperature,sizeof(temperature),"%.2f",(double)sample.temperature_c);
                snprintf(humidity,sizeof(humidity),"%.2f",(double)sample.humidity_pct);
            }
            if (sample.light_valid) snprintf(light,sizeof(light),"%lu",(unsigned long)sample.light_lux);
            snprintf(line,sizeof(line),
              "{\"os\":\"microTKernel3SMP\",\"sequence\":%lu,\"timestamp_ms\":%lu,"
              "\"sensor_processor\":%d,\"logger_processor\":%d,\"distance_cm\":%s,"
              "\"distance_status\":\"%s\",\"echo_us\":%lu,\"temperature_c\":%s,"
              "\"humidity_pct\":%s,\"aht_status\":\"%s\",\"aht_i2c_error\":%d,"
              "\"light_lux\":%s,\"light_status\":\"%s\",\"light_i2c_error\":%d,"
              "\"display_connected\":null,\"display_enabled\":false}\n",
              (unsigned long)sample.sequence,(unsigned long)sample.timestamp_ms,
              sample.acquisition_processor,(int)tk_get_prc(),distance,sample.distance_status,
              (unsigned long)sample.echo_us,temperature,humidity,sample.aht_status,
              sample.aht_i2c_error,light,sample.light_status,sample.light_i2c_error);
            tm_printf((UB*)"%s",line); last_sequence=sample.sequence;
        }
        tk_dly_tsk(50);
    }
}
static ER launch(FP entry,PRI priority,SZ stack,UW processor,bool fpu) {
    T_CTSK task={0}; task.task=entry; task.itskpri=priority; task.stksz=stack;
    task.tskatr=TA_HLNG|TA_RNG0|TA_ASSPRC|(fpu ? TA_FPU : 0);
    task.assprc=processor;
    ID id=tk_cre_tsk(&task); if (id<0) return id;
    return tk_sta_tsk(id,0);
}
EXPORT INT usermain(void) {
    T_CMTX mutex={.mtxatr=TA_INHERIT};
    snapshot_mutex=tk_cre_mtx(&mutex);
    if (snapshot_mutex<0) { tm_printf((UB*)"FATAL mutex: %d\n",snapshot_mutex); return -1; }
    tm_printf((UB*)"AAI15 fresh sensor target: micro T-Kernel SMP; LCD disabled\n");
    tm_printf((UB*)"Pins SDA=1 SCL=2 TRIG=4 ECHO=5; processor 1=CPU0, processor 2=CPU1\n");
    ER rc=launch((FP)idle_task,CNF_MAX_TSKPRI,1024,TP_PRC1,false);
    if (rc==E_OK) rc=launch((FP)idle_task,CNF_MAX_TSKPRI,1024,TP_PRC2,false);
    if (rc==E_OK) rc=launch((FP)sensor_task,5,6144,SENSOR_PROCESSOR,true);
    if (rc==E_OK) rc=launch((FP)logger_task,10,6144,LOGGER_PROCESSOR,true);
    if (rc!=E_OK) { tm_printf((UB*)"FATAL task launch: %d\n",rc); return -1; }
    tk_slp_tsk(TMO_FEVR); return 0;
}
