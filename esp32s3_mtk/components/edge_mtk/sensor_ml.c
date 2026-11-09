#include "sensor_ml.h"
#include "application/sensor_measurement.h"
#include "application/scheduler.h"
#include "inference_engine/inference_manager.h"
#include "inference_engine/tflite_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "sine_models.h"

/* These immutable bytes already belong to inference_manager.c. */
extern const unsigned char g_autoencoder_model_data[];
extern const int g_autoencoder_model_size;
typedef struct { const char *id; const unsigned char *bytes; unsigned size; int count; } model_t;
static const model_t models[]={
    {"sine-float",ml_sine_float,sizeof ml_sine_float,1},
    {"sine-int8",ml_sine_int8,sizeof ml_sine_int8,1},
    {"autoencoder",g_autoencoder_model_data,16440,16}
};
typedef struct {
    bool bound,valid;
    char source[32],field[24];
    float lo,hi,value,raw;
    uint32_t at;
} binding_t;
static binding_t bindings[16];
static int active=-1;
static bool enabled;
static uint32_t generation,revision,last_ms;
static uint32_t interval_ms=2000,max_age_ms=5000,max_skew_ms=2500;

void sensor_ml_invalidate(void) {
    for(int i=0;i<16;i++) bindings[i].valid=false;
    generation++;
    last_ms=hal_time_ms()-interval_ms;
}
void sensor_measurement_notify(const app_cfg_t *cfg,const uint8_t *data,int length) {
    for(int i=0;i<16;i++) {
        binding_t *b=&bindings[i];
        if(!b->bound || strcmp(b->source,cfg->name)) continue;
        float value;
        b->valid=sensor_measurement_value(cfg,data,length,b->field,&value);
        if(b->valid) {
            b->raw=value;
            b->value=fminf(1,fmaxf(0,(value-b->lo)/(b->hi-b->lo)));
            b->at=hal_time_ms();
        }
    }
}
static bool identifier(const char *s) {
    if(!*s) return false;
    for(;*s;s++) if(!isalnum((unsigned char)*s) && *s!='_' && *s!='-') return false;
    return true;
}
static bool field_supported(const char *source,const char *field) {
    const app_cfg_t *c=scheduler_config_by_name(source);
    return c && sensor_measurement_field_supported(c,field);
}
static int bound_count(void) {
    int n=0;
    while(n<16 && bindings[n].bound) n++;
    for(int i=n;i<16;i++) if(bindings[i].bound) return -1;
    return n;
}
static bool select_model(int index) {
    int old=active;
    if(!inference_mgr_load_bytes(models[index].bytes,models[index].size)) {
        if(old>=0 && !inference_mgr_load_bytes(models[old].bytes,models[old].size)) active=-1;
        enabled=false;
        sensor_ml_invalidate();
        return false;
    }
    active=index;
    revision=inference_mgr_revision();
    enabled=false;
    sensor_ml_invalidate();
    return true;
}
static void show(void) {
    printf("ML: model=%s enabled=%u generation=%lu interval_ms=%lu max_age_ms=%lu max_skew_ms=%lu\n",
        active<0 ? "none":models[active].id,enabled,(unsigned long)generation,
        (unsigned long)interval_ms,(unsigned long)max_age_ms,(unsigned long)max_skew_ms);
    for(int i=0;i<16;i++) if(bindings[i].bound)
        printf("ML: bind=%d source=%s field=%s min=%.6g max=%.6g valid=%u\n",i,
            bindings[i].source,bindings[i].field,(double)bindings[i].lo,(double)bindings[i].hi,bindings[i].valid);
}
bool sensor_ml_command(const char *line,bool sampling) {
    if(strncmp(line,"ML",2) || (line[2] && line[2]!=' ')) return false;
    if(!strcmp(line,"ML") || !strcmp(line,"ML HELP")) {
        puts("ML: HELP | SHOW | PRESET [sine-float|sine-int8|autoencoder] | MODEL <id> | RESET | BIND <index> <source> <field> <min> <max> | UNBIND <index> | TIMING <interval_ms> <age_ms> <skew_ms> | RUN | STOP");
        return true;
    }
    if(!strcmp(line,"ML SHOW")) { show(); return true; }
    if(!strcmp(line,"ML STOP")) { enabled=false; sensor_ml_invalidate(); puts("ML: OK STOP"); return true; }
    if(sampling) { puts("ML: ERROR send STOP before changing ML configuration"); return true; }
    char id[32],source[32],field[24],extra;
    int i; float lo,hi;
    unsigned period,age,skew;
    if(!strcmp(line,"ML RESET")) {
        memset(bindings,0,sizeof bindings); enabled=false; sensor_ml_invalidate();
    } else if(sscanf(line,"ML MODEL %31s %c",id,&extra)==1 || !strcmp(line,"ML PRESET") ||
              sscanf(line,"ML PRESET %31s %c",id,&extra)==1) {
        bool preset=!strncmp(line,"ML PRESET",9);
        if(!strcmp(line,"ML PRESET")) strcpy(id,"sine-float");
        int m; for(m=0;m<3;m++) if(!strcmp(id,models[m].id)) break;
        if(m==3) goto invalid;
        if(preset && (!field_supported("AHT","temperature_c") || !field_supported("AHT","humidity_pct") ||
            !field_supported("LIGHT","light_lux") || !field_supported("DISTANCE","distance_cm"))) goto invalid;
        if(!select_model(m)) { puts("ML: ERROR model load failed; pipeline disabled"); return true; }
        if(preset) {
            static const char *src[]={"AHT","AHT","LIGHT","DISTANCE"};
            static const char *fld[]={"temperature_c","humidity_pct","light_lux","distance_cm"};
            static const float lows[]={-10,0,0,2}, highs[]={50,100,1000,400};
            memset(bindings,0,sizeof bindings);
            for(int k=0;k<(m==2 ? 16:4);k++) {
                binding_t *b=&bindings[k]; b->bound=true;
                strcpy(b->source,src[k%4]); strcpy(b->field,fld[k%4]); b->lo=lows[k%4]; b->hi=highs[k%4];
            }
        }
    } else if(sscanf(line,"ML BIND %9d %31s %23s %f %f %c",&i,source,field,&lo,&hi,&extra)==5) {
        if(i<0 || i>=16 || !identifier(source) || !identifier(field) || !isfinite(lo) || !isfinite(hi) ||
            hi<=lo || !isfinite(hi-lo) || !field_supported(source,field)) goto invalid;
        binding_t b={.bound=true,.lo=lo,.hi=hi}; strcpy(b.source,source); strcpy(b.field,field); bindings[i]=b;
        enabled=false; sensor_ml_invalidate();
    } else if(sscanf(line,"ML UNBIND %9d %c",&i,&extra)==1) {
        if(i<0 || i>=16) goto invalid;
        memset(&bindings[i],0,sizeof bindings[i]); enabled=false; sensor_ml_invalidate();
    } else if(sscanf(line,"ML TIMING %9u %9u %9u %c",&period,&age,&skew,&extra)==3) {
        if(period<100 || period>60000 || age<100 || age>60000 || skew>age) goto invalid;
        interval_ms=period; max_age_ms=age; max_skew_ms=skew; enabled=false; sensor_ml_invalidate();
    } else if(!strcmp(line,"ML RUN")) {
        int n=bound_count();
        if(active<0 || n<=0 || (models[active].count!=1 && n!=models[active].count) ||
           revision!=inference_mgr_revision() || !inference_mgr_is_ready()) goto invalid;
        for(int k=0;k<n;k++) if(!field_supported(bindings[k].source,bindings[k].field)) goto invalid;
        enabled=true; sensor_ml_invalidate();
    } else goto invalid;
    printf("ML: OK %s\n",line+3);
    return true;
invalid:
    puts("ML: ERROR invalid command, model, field, binding layout or range; use ML HELP/SHOW");
    return true;
}
static void skip(const char *reason,uint32_t now) {
    printf("{\"type\":\"ml\",\"timestamp_ms\":%lu,\"generation\":%lu,\"model\":\"%s\",\"outputs\":null,\"status\":\"%s\"}\n",
        (unsigned long)now,(unsigned long)generation,models[active].id,reason);
}
void sensor_ml_poll(bool sampling) {
    uint32_t now=hal_time_ms();
    if(!sampling || !enabled || active<0 || now-last_ms<interval_ms) return;
    last_ms=now;
    if(revision!=inference_mgr_revision()) { skip("MODEL_CHANGED",now); enabled=false; return; }
    int n=bound_count();
    if(n<=0) { skip("INVALID_MAPPING",now); return; }
    uint32_t oldest=0,newest=UINT32_MAX;
    float inputs[16]={0};
    for(int i=0;i<n;i++) {
        if(!bindings[i].valid || now-bindings[i].at>max_age_ms) { skip("MISSING_OR_STALE",now); return; }
        uint32_t age=now-bindings[i].at;
        if(age>oldest) oldest=age;
        if(age<newest) newest=age;
        inputs[i]=bindings[i].value;
    }
    if(oldest-newest>max_skew_ms) { skip("SAMPLE_SKEW",now); return; }
    int count=models[active].count;
    if(count==1) { float sum=0; for(int i=0;i<n;i++) sum+=inputs[i]; inputs[0]=(sum/n)*6.28318530718f; }
    uint64_t start=hal_time_us();
    if(!inference_mgr_set_inputs(inputs,count) || !inference_mgr_run()) { skip("INVOKE_ERROR",now); return; }
    uint64_t elapsed=hal_time_us()-start;
    float outputs[16]; int out_count=inference_mgr_output_count();
    if(out_count<1 || out_count>16) { skip("OUTPUT_ERROR",now); return; }
    for(int i=0;i<out_count;i++) if(!inference_mgr_get_output(i,&outputs[i]) || !isfinite(outputs[i])) { skip("OUTPUT_ERROR",now); return; }
    /* One line assembled before printf so other tasks cannot interleave fragments. */
    char line[1800];
    int used=snprintf(line,sizeof line,"{\"type\":\"ml\",\"timestamp_ms\":%lu,\"generation\":%lu,\"model\":\"%s\",\"raw_features\":[",
        (unsigned long)now,(unsigned long)generation,models[active].id);
    for(int i=0;i<n;i++) used+=snprintf(line+used,sizeof line-(size_t)used,"%s%.7g",i ? ",":"",(double)bindings[i].raw);
    used+=snprintf(line+used,sizeof line-(size_t)used,"],\"inputs\":[");
    for(int i=0;i<count;i++) used+=snprintf(line+used,sizeof line-(size_t)used,"%s%.7g",i ? ",":"",(double)inputs[i]);
    used+=snprintf(line+used,sizeof line-(size_t)used,"],\"outputs\":[");
    for(int i=0;i<out_count;i++) used+=snprintf(line+used,sizeof line-(size_t)used,"%s%.7g",i ? ",":"",(double)outputs[i]);
    snprintf(line+used,sizeof line-(size_t)used,"],\"invoke_us\":%lu,\"status\":\"OK\"}",(unsigned long)elapsed);
    puts(line);
}
