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
#include <errno.h>
#include "sine_models.h"

/* These immutable bytes already belong to inference_manager.c. */
extern const unsigned char g_autoencoder_model_data[];
extern const int g_autoencoder_model_size;
extern const uint8_t _binary_comfort_summer_tflite_start[], _binary_comfort_summer_tflite_end[];
#ifdef HAVE_PEOPLE_COUNTER_MODEL
extern const uint8_t _binary_people_counter_raw_int8_tflite_start[], _binary_people_counter_raw_int8_tflite_end[];
#endif

typedef enum { SINE, RECONSTRUCTION, COMFORT, DIRECTION } model_kind_t;
typedef struct {
    const char *id;
    const uint8_t *bytes;
    const uint8_t *end;
    unsigned size;
    int count, outputs;
    model_kind_t kind;
} model_t;
static const model_t models[] = {
    {"sine-float", ml_sine_float, NULL, sizeof ml_sine_float, 1, 1, SINE},
    {"sine-int8", ml_sine_int8, NULL, sizeof ml_sine_int8, 1, 1, SINE},
    {"autoencoder", g_autoencoder_model_data, NULL, 16440, 16, 16, RECONSTRUCTION},
    {"comfort-summer", _binary_comfort_summer_tflite_start, _binary_comfort_summer_tflite_end, 0, 2, 100, COMFORT},
#ifdef HAVE_PEOPLE_COUNTER_MODEL
    {"people-counter", _binary_people_counter_raw_int8_tflite_start, _binary_people_counter_raw_int8_tflite_end, 0, 31, 3, DIRECTION},
#else
    {"people-counter", NULL, NULL, 0, 31, 3, DIRECTION},
#endif
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
static bool threshold_enabled;
static float output_threshold;
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
static bool load_model(const model_t *model) {
    uint32_t size = model->end ? (uint32_t)((uintptr_t)model->end - (uintptr_t)model->bytes) : model->size;
    return model->bytes && inference_mgr_load_bytes(model->bytes, size) &&
        inference_mgr_input_count() == model->count && inference_mgr_output_count() == model->outputs;
}

static bool select_model(int index) {
    int old=active;
    if(!load_model(&models[index])) {
        if (old >= 0 && load_model(&models[old])) revision = inference_mgr_revision();
        else active = -1;
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
static bool set_threshold(const char *value) {
    while (*value == ' ') value++;
    if (!strcmp(value, "OFF")) {
        threshold_enabled = false;
        return true;
    }
    char *end;
    errno = 0;
    float cutoff = strtof(value, &end);
    if (end == value || *end || errno == ERANGE || !isfinite(cutoff)) {
        puts("ML: ERROR expected ML THRESHOLD <finite value> or ML THRESHOLD OFF");
        return false;
    }
    output_threshold = cutoff;
    threshold_enabled = true;
    return true;
}

static void show(void) {
    printf("ML: model=%s enabled=%u generation=%lu interval_ms=%lu max_age_ms=%lu max_skew_ms=%lu\n",
        active<0 ? "none":models[active].id,enabled,(unsigned long)generation,
        (unsigned long)interval_ms,(unsigned long)max_age_ms,(unsigned long)max_skew_ms);
    if (threshold_enabled) printf("ML: threshold=%.9g (scalar output)\n", (double)output_threshold);
    else puts("ML: threshold=off");
    for(int i=0;i<16;i++) if(bindings[i].bound)
        printf("ML: bind=%d source=%s field=%s min=%.6g max=%.6g valid=%u\n",i,
            bindings[i].source,bindings[i].field,(double)bindings[i].lo,(double)bindings[i].hi,bindings[i].valid);
}
static void skip(const char *reason, uint32_t now);

/* The published comfort model encodes the score as the winning class index. */
static const char *class_fields(const model_t *model, int winner, float confidence,
                                char *buf, size_t capacity) {
    if (model->kind == COMFORT)
        snprintf(buf, capacity, ",\"comfort_score\":%d,\"class_score\":%.7g", winner, (double)confidence);
    else {
        static const char *labels[] = {"in", "none", "out"};
        snprintf(buf, capacity, ",\"direction\":\"%s\",\"class_score\":%.7g", labels[winner], (double)confidence);
    }
    return buf;
}

static bool class_result(const model_t *model, const float *inputs, const char *source, uint32_t now) {
    uint64_t start = hal_time_us();
    if (!inference_mgr_set_inputs(inputs, model->count) || !inference_mgr_run()) {
        skip("INVOKE_ERROR", now);
        return false;
    }
    uint64_t elapsed = hal_time_us() - start;
    int winner = 0;
    float confidence = -1.0f;
    for (int i = 0; i < model->outputs; i++) {
        float value;
        if (!inference_mgr_get_output(i, &value) || !isfinite(value) || value < 0.0f || value > 1.0f) {
            skip("OUTPUT_ERROR", now);
            return false;
        }
        if (value > confidence) {
            winner = i;
            confidence = value;
        }
    }
    char fields[112];
    printf("{\"type\":\"ml\",\"timestamp_ms\":%lu,\"generation\":%lu,\"model\":\"%s\","
           "\"input_source\":\"%s\",\"outputs\":[%d]%s,\"invoke_us\":%lu,\"status\":\"OK\"}\n",
           (unsigned long)now, (unsigned long)generation, model->id, source, winner,
           class_fields(model, winner, confidence, fields, sizeof fields), (unsigned long)elapsed);
    return true;
}

static bool scalar_or_vector_result(const float *inputs, int count, int raw_count,
                                    const char *source, uint32_t now) {
    uint64_t start=hal_time_us();
    if(!inference_mgr_set_inputs(inputs,count) || !inference_mgr_run()) { skip("INVOKE_ERROR",now); return false; }
    uint64_t elapsed=hal_time_us()-start;
    float outputs[16]; int out_count=inference_mgr_output_count();
    if(out_count<1 || out_count>16) { skip("OUTPUT_ERROR",now); return false; }
    for(int i=0;i<out_count;i++) if(!inference_mgr_get_output(i,&outputs[i]) || !isfinite(outputs[i])) { skip("OUTPUT_ERROR",now); return false; }
    /* One line assembled before printf so other tasks cannot interleave fragments. */
    char line[1800];
    int used=snprintf(line,sizeof line,"{\"type\":\"ml\",\"timestamp_ms\":%lu,\"generation\":%lu,\"model\":\"%s\",\"raw_features\":[",
        (unsigned long)now,(unsigned long)generation,models[active].id);
    for(int i=0;i<raw_count;i++) used+=snprintf(line+used,sizeof line-(size_t)used,"%s%.7g",i ? ",":"",(double)bindings[i].raw);
    used+=snprintf(line+used,sizeof line-(size_t)used,"],\"input_source\":\"%s\",\"inputs\":[", source);
    for(int i=0;i<count;i++) used+=snprintf(line+used,sizeof line-(size_t)used,"%s%.7g",i ? ",":"",(double)inputs[i]);
    used+=snprintf(line+used,sizeof line-(size_t)used,"],\"outputs\":[");
    bool compare_output = threshold_enabled && out_count == 1;
    for(int i=0;i<out_count;i++) used+=snprintf(line+used,sizeof line-(size_t)used,"%s%.*g",
        i ? ",":"", compare_output ? 9 : 7, (double)outputs[i]);
    used += snprintf(line + used, sizeof line - (size_t)used, "]");
    if (compare_output) {
        used += snprintf(line + used, sizeof line - (size_t)used,
            ",\"threshold\":%.9g,\"threshold_exceeded\":%s",
            (double)output_threshold, outputs[0] > output_threshold ? "true" : "false");
    }
    snprintf(line+used,sizeof line-(size_t)used,",\"invoke_us\":%lu,\"status\":\"OK\"}",(unsigned long)elapsed);
    puts(line);
    return true;
}

static bool replay(const char *values) {
    if (active < 0 || revision != inference_mgr_revision() || !inference_mgr_is_ready()) return false;
    const model_t *model = &models[active];
    float inputs[31];
    for (int i = 0; i < model->count; i++) {
        char *end;
        if (!isspace((unsigned char)*values)) return false;
        errno = 0;
        inputs[i] = strtof(values, &end);
        if (end == values || errno == ERANGE || !isfinite(inputs[i])) return false;
        values = end;
    }
    while (isspace((unsigned char)*values)) values++;
    if (*values) return false;
    uint32_t now = hal_time_ms();
    if (model->kind == COMFORT || model->kind == DIRECTION)
        return class_result(model, inputs, "replay", now);
    return scalar_or_vector_result(inputs, model->count, 0, "replay", now);
}

bool sensor_ml_command(const char *line,bool sampling) {
    if(strncmp(line,"ML",2) || (line[2] && line[2]!=' ')) return false;
    if(!strcmp(line,"ML") || !strcmp(line,"ML HELP")) {
        puts("ML: HELP | SHOW | LIST | PRESET <id> | MODEL <id> | REPLAY <model-ready values> | RESET | BIND <index> <source> <field> <min> <max> | UNBIND <index> | TIMING <interval_ms> <age_ms> <skew_ms> | THRESHOLD <value|OFF> | RUN | STOP");
        return true;
    }
    if (!strcmp(line, "ML LIST")) {
        for (size_t m = 0; m < sizeof models / sizeof models[0]; m++)
            printf("ML: %s inputs=%d outputs=%d %s\n", models[m].id, models[m].count,
                   models[m].outputs, models[m].bytes ? "available" : "local export required");
        return true;
    }
    if (!strncmp(line, "ML REPLAY ", 10)) {
        if (!sampling && replay(line + 9)) printf("ML: OK %s\n", line + 3);
        else puts("ML: ERROR send STOP, select a model and supply its exact replay input vector");
        return true;
    }
    if(!strcmp(line,"ML SHOW")) { show(); return true; }
    if(!strcmp(line,"ML STOP")) { enabled=false; sensor_ml_invalidate(); puts("ML: OK STOP"); return true; }
    const char threshold_command[] = "ML THRESHOLD";
    const size_t threshold_len = sizeof threshold_command - 1;
    if (!strncmp(line, threshold_command, threshold_len) &&
        (!line[threshold_len] || line[threshold_len] == ' ')) {
        if (set_threshold(line + threshold_len)) printf("ML: OK %s\n", line + 3);
        return true;
    }
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
        int m;
        const int model_count = sizeof models / sizeof models[0];
        for (m = 0; m < model_count; m++) if (!strcmp(id, models[m].id)) break;
        if (m == model_count) goto invalid;
        const model_t *model = &models[m];
        if (!model->bytes) {
            puts("ML: ERROR people-counter local model export is missing from this build");
            return true;
        }
        if (preset && model->kind != COMFORT && model->kind != DIRECTION &&
            (!field_supported("AHT","temperature_c") || !field_supported("AHT","humidity_pct") ||
             !field_supported("LIGHT","light_lux") || !field_supported("DISTANCE","distance_cm"))) goto invalid;
        if(!select_model(m)) { puts("ML: ERROR model load failed; pipeline disabled"); return true; }
        if (preset && model->kind == COMFORT) {
            memset(bindings, 0, sizeof bindings);
            bindings[0] = (binding_t){.bound = true, .source = "AHT", .field = "temperature_c", .lo = 0, .hi = 60};
            bindings[1] = (binding_t){.bound = true, .source = "AHT", .field = "humidity_pct", .lo = 0, .hi = 100};
        } else if (preset && model->kind == DIRECTION) {
            memset(bindings, 0, sizeof bindings);
        } else if(preset) {
            static const char *src[]={"AHT","AHT","LIGHT","DISTANCE"};
            static const char *fld[]={"temperature_c","humidity_pct","light_lux","distance_cm"};
            static const float lows[]={-10,0,0,2}, highs[]={50,100,1000,400};
            memset(bindings,0,sizeof bindings);
            for(int k=0;k<(model->kind==RECONSTRUCTION ? 16:4);k++) {
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
        if (active < 0 || n <= 0 || models[active].kind == DIRECTION ||
            (models[active].kind != SINE && n != models[active].count) ||
            revision != inference_mgr_revision() || !inference_mgr_is_ready()) goto invalid;
        if (models[active].kind == COMFORT &&
            (strcmp(bindings[0].field, "temperature_c") || strcmp(bindings[1].field, "humidity_pct"))) goto invalid;
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
        inputs[i] = models[active].kind == COMFORT ? bindings[i].raw : bindings[i].value;
    }
    if(oldest-newest>max_skew_ms) { skip("SAMPLE_SKEW",now); return; }
    const model_t *model = &models[active];
    if (model->kind == COMFORT) {
        class_result(model, inputs, "sensors", now);
        return;
    }
    int count=model->count;
    if(model->kind==SINE) { float sum=0; for(int i=0;i<n;i++) sum+=inputs[i]; inputs[0]=(sum/n)*6.28318530718f; }
    scalar_or_vector_result(inputs, count, n, "sensors", now);
}
