#include "sensor_ml.h"
#include "application/sensor_measurement.h"
#include "application/scheduler.h"
#include "inference_engine/inference_manager.h"
#include "inference_engine/tflite_wrapper.h"
#include "third_party/jsmn/jsmn.h"
#include "application/output_format.h"
#include "mqtt/mqtt_model_transfer.h"
#include "ml_mqtt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include "sine_models.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

/* These immutable bytes already belong to inference_manager.c. */
extern const unsigned char g_autoencoder_model_data[];
extern const int g_autoencoder_model_size;
extern const uint8_t _binary_comfort_summer_tflite_start[], _binary_comfort_summer_tflite_end[];
#ifdef HAVE_PEOPLE_COUNTER_MODEL
extern const uint8_t _binary_people_counter_raw_int8_tflite_start[], _binary_people_counter_raw_int8_tflite_end[];
#endif

/* UPLOADED is the model last sent over MQTT into flash (D1 task 31); its
   shape is read from the model itself when it is loaded. */
typedef enum { SINE, RECONSTRUCTION, COMFORT, DIRECTION, UPLOADED } model_kind_t;
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
    {"uploaded", NULL, NULL, 0, 0, 0, UPLOADED},
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
/* Where results go besides the console: MQTT ml/result, or this board's own
   rules as the sensor ML (D4 task 3.4). */
static bool publish_output, local_output;

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
static const uint8_t *model_bytes(const model_t *model, uint32_t *size) {
    if (model->kind == UPLOADED) {
        *size = model_transfer_model_size();
        const uint8_t *bytes = model_transfer_get_model_ptr();
        return bytes && *size ? bytes : NULL;
    }
    *size = model->end ? (uint32_t)((uintptr_t)model->end - (uintptr_t)model->bytes) : model->size;
    return model->bytes;
}
static bool model_available(const model_t *model) {
    uint32_t size;
    return model_bytes(model, &size) != NULL;
}
/* An uploaded model's input count is only known once it is the loaded model. */
static int model_inputs(const model_t *model) {
    return model->kind == UPLOADED ? inference_mgr_input_count() : model->count;
}
static bool load_model(const model_t *model) {
    uint32_t size;
    const uint8_t *bytes = model_bytes(model, &size);
    if (!bytes || !inference_mgr_load_bytes(bytes, size)) return false;
    return model->kind == UPLOADED ||
        (inference_mgr_input_count() == model->count && inference_mgr_output_count() == model->outputs);
}
/* n contiguous bindings fit the model: the sine models and a one-input
   uploaded model average any number into their single input. */
static bool shape_ok(const model_t *model, int n, const binding_t *b) {
    if (n <= 0 || n > 16 || model->kind == DIRECTION) return false;
    if (model->kind == SINE) return true;
    int need = model_inputs(model);
    if (model->kind == UPLOADED) return need == 1 || n == need;
    if (n != need) return false;
    return model->kind != COMFORT ||
        (!strcmp(b[0].field, "temperature_c") && !strcmp(b[1].field, "humidity_pct"));
}
/* Where the bound readings come from, for the result line: "sensors" when all
   are this board's, "remote:<node>" when all arrive from one followed node. */
static const char *input_label(int n, char *buf, size_t capacity) {
    const char *node = NULL;
    bool local = false, mixed = false;
    for (int i = 0; i < n; i++) {
        const app_cfg_t *c = scheduler_config_by_name(bindings[i].source);
        if (c && c->proto == proto_mqtt) {
            if (node && strcmp(node, c->remote_node)) mixed = true;
            node = c->remote_node;
        } else local = true;
    }
    if (!node) return "sensors";
    if (local || mixed || !identifier(node)) return "mixed";
    snprintf(buf, capacity, "remote:%s", node);
    return buf;
}
static const char *output_name(void) {
    return local_output ? "local" : publish_output ? "publish" : "console";
}
/* D4 task 3.4: the result becomes this board's sensor ML, so rules with
   source=ML act on it and event-driven sensors can trigger on it, exactly as
   for a hardware reading; like every sensor it is also on
   pico/<node>/sensor/ML, so another board can follow it. A class model gives
   its class index (comfort-summer: the score) as u16le(0); other models give
   each output times 1000 as s16le(2*i), clamped to the 16-bit range. */
static const app_cfg_t ml_source = { .name = "ML", .proto = proto_mqtt };
static void emit_local(const float *values, int count, bool class_index) {
    uint8_t frame[32];
    int n = 0;
    for (int i = 0; i < count && n + 2 <= (int)sizeof frame; i++) {
        float v = class_index ? values[i] : values[i] * 1000.0f;
        int16_t q = (int16_t)lroundf(fminf(32767.0f, fmaxf(-32768.0f, v)));
        frame[n++] = (uint8_t)((uint16_t)q & 0xff);
        frame[n++] = (uint8_t)((uint16_t)q >> 8);
    }
    output_emit_hex(&ml_source, "ml", "ML", frame, n);
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
    publish_output=false;
    local_output=false;
    sensor_ml_invalidate();
    return true;
}

/* D4 Part 2: parse a complete candidate before changing the running pipeline.
   jsmn's parent links let us reject duplicate and unknown direct keys. */
static const char *setup_json;
/* 6.4 KB of tokens, used only by SchedulerTask while parsing a setup, so they
   live in PSRAM: in internal RAM they overflowed the N16R8 build by 2,816 B. */
EXT_RAM_BSS_ATTR static jsmntok_t setup_tokens[320];
static int setup_ntokens;

static bool token_eq(int t, const char *s) {
    return t >= 0 && setup_tokens[t].type == JSMN_STRING &&
        (int)strlen(s) == setup_tokens[t].end - setup_tokens[t].start &&
        !strncmp(setup_json + setup_tokens[t].start, s, strlen(s));
}
static bool token_copy(int t, char *out, size_t capacity) {
    if (t < 0 || setup_tokens[t].type != JSMN_STRING) return false;
    int n = setup_tokens[t].end - setup_tokens[t].start;
    if (n <= 0 || (size_t)n >= capacity ||
        memchr(setup_json + setup_tokens[t].start, '\\', (size_t)n)) return false;
    memcpy(out, setup_json + setup_tokens[t].start, (size_t)n);
    out[n] = 0;
    return true;
}
static bool token_number(int t, float *out) {
    if (t < 0 || setup_tokens[t].type != JSMN_PRIMITIVE) return false;
    int n = setup_tokens[t].end - setup_tokens[t].start;
    char buf[32];
    if (n < 1 || (size_t)n >= sizeof buf) return false;
    memcpy(buf, setup_json + setup_tokens[t].start, (size_t)n);
    buf[n] = 0;
    char *end;
    errno = 0;
    *out = strtof(buf, &end);
    return end != buf && !*end && errno != ERANGE && isfinite(*out);
}
static int token_raw_start(int t) {
    return setup_tokens[t].start - (setup_tokens[t].type == JSMN_STRING ? 1 : 0);
}
static int token_raw_end(int t) {
    return setup_tokens[t].end + (setup_tokens[t].type == JSMN_STRING ? 1 : 0);
}
static bool separator(int from, int to, char expected) {
    while (from < to && isspace((unsigned char)setup_json[from])) from++;
    if (expected && from < to && setup_json[from] == expected) from++;
    else if (expected) return false;
    while (from < to && isspace((unsigned char)setup_json[from])) from++;
    return from == to;
}
static int object_field(int object, const char *key) {
    if (object < 0 || setup_tokens[object].type != JSMN_OBJECT) return -1;
    int found = -1, prev = -1;
    for (int t = object + 1; t < setup_ntokens && setup_tokens[t].start < setup_tokens[object].end; t++) {
        if (setup_tokens[t].parent != object) continue;
        if (prev < 0) { prev = t; continue; }
        if (token_eq(prev, key)) {
            if (found >= 0) return -2;
            found = t;
        }
        prev = -1;
    }
    return prev < 0 ? found : -2;
}
static bool object_keys(int object, const char *const *keys, int count) {
    if (object < 0 || setup_tokens[object].type != JSMN_OBJECT) return false;
    int children = 0, key = -1, previous_value = -1;
    for (int t = object + 1; t < setup_ntokens && setup_tokens[t].start < setup_tokens[object].end; t++) {
        if (setup_tokens[t].parent != object) continue;
        if (key < 0) {
            if (setup_tokens[t].type != JSMN_STRING ||
                !separator(previous_value < 0 ? setup_tokens[object].start + 1 : token_raw_end(previous_value),
                           token_raw_start(t), previous_value < 0 ? 0 : ',')) return false;
            key = t;
        } else {
            if (!separator(token_raw_end(key), token_raw_start(t), ':')) return false;
            previous_value = t;
            key = -1;
        }
        children++;
    }
    if (key >= 0 || !separator(previous_value < 0 ? setup_tokens[object].start + 1 : token_raw_end(previous_value),
                               setup_tokens[object].end - 1, 0)) return false;
    if (children != count * 2) return false;
    for (int k = 0; k < count; k++) if (object_field(object, keys[k]) < 0) return false;
    return true;
}
/* A local source names a sensor configured on this board. A remote source
   names another board's node and sensor (D4 task 3.3); its readings arrive
   through the proto=mqtt sensor that follows it, whose remote_profile decodes
   them, so a remote input is never a local sensor under another name. */
static bool parse_binding(int item, int position, binding_t *out, const char **reason) {
    static const char *const keys[] = {"index", "source", "field", "min", "max"};
    static const char *const local_keys[] = {"kind", "name"};
    static const char *const remote_keys[] = {"kind", "node", "name"};
    if (!object_keys(item, keys, 5)) return false;
    float index;
    if (!token_number(object_field(item, "index"), &index) || index != position) return false;
    int source = object_field(item, "source");
    bool remote = token_eq(object_field(source, "kind"), "remote");
    if (remote) *reason = "invalid_or_unavailable_remote_input";
    if (remote ? !object_keys(source, remote_keys, 3)
               : (!object_keys(source, local_keys, 2) || !token_eq(object_field(source, "kind"), "local")))
        return false;
    binding_t b = {.bound = true};
    char name[32], node[32];
    if (!token_copy(object_field(source, "name"), name, sizeof name) || !identifier(name) ||
        !token_copy(object_field(item, "field"), b.field, sizeof b.field) || !identifier(b.field) ||
        !token_number(object_field(item, "min"), &b.lo) ||
        !token_number(object_field(item, "max"), &b.hi) ||
        b.hi <= b.lo || !isfinite(b.hi - b.lo)) return false;
    const app_cfg_t *c;
    if (remote) {
        if (!token_copy(object_field(source, "node"), node, sizeof node) || !identifier(node)) return false;
        c = scheduler_config_following(node, name);
        if (!c) { *reason = "remote_source_not_followed"; return false; }
    } else {
        c = scheduler_config_by_name(name);
        if (c && c->proto == proto_mqtt) return false;
    }
    if (!c || strlen(c->name) >= sizeof b.source) return false;
    strcpy(b.source, c->name);
    if (!field_supported(b.source, b.field)) {
        if (remote) *reason = "remote_field_not_decoded";
        return false;
    }
    *out = b;
    return true;
}
static bool setup_error(const char *reason) {
    printf("ML SETUP: ERROR %s\n", reason);
    mqtt_pub_ml_status(false, reason, (unsigned long)generation);
    return true;
}
bool sensor_ml_setup_json(const char *line, bool sampling) {
    (void)sampling; /* Sensor sampling deliberately continues across a swap. */
    if (!line || *line != '{' || !strstr(line, "\"ml_setup\"")) return false;
    size_t length = strlen(line);
    if (length >= 2048) return setup_error("command_too_long");
    setup_json = line;
    jsmn_parser parser;
    jsmn_init(&parser);
    setup_ntokens = jsmn_parse(&parser, line, length, setup_tokens,
                               sizeof setup_tokens / sizeof setup_tokens[0]);
    static const char *const root_keys[] = {"ml_setup"};
    static const char *const setup_keys[] = {"version", "model", "inputs", "output"};
    static const char *const output_keys[] = {"kind"};
    float version;
    int setup = setup_ntokens > 0 ? object_field(0, "ml_setup") : -1;
    if (setup_ntokens < 1 || setup_tokens[0].start != 0 ||
        setup_tokens[0].end != (int)length || !object_keys(0, root_keys, 1) ||
        !object_keys(setup, setup_keys, 4) ||
        !token_number(object_field(setup, "version"), &version) || version != 1) {
        return setup_error("invalid_json_or_version");
    }
    char id[32];
    if (!token_copy(object_field(setup, "model"), id, sizeof id)) {
        return setup_error("model_id_required");
    }
    int model_index = -1;
    for (size_t m = 0; m < sizeof models / sizeof models[0]; m++)
        if (!strcmp(id, models[m].id)) { model_index = (int)m; break; }
    if (model_index < 0 || models[model_index].kind == DIRECTION) {
        return setup_error("unsupported_live_model");
    }
    if (!model_available(&models[model_index])) {
        return setup_error(models[model_index].kind == UPLOADED ? "no_uploaded_model" : "unsupported_live_model");
    }
    int output = object_field(setup, "output");
    int output_kind = object_keys(output, output_keys, 1) ? object_field(output, "kind") : -1;
    if (!token_eq(output_kind, "publish") && !token_eq(output_kind, "console") &&
        !token_eq(output_kind, "local")) {
        return setup_error("unsupported_output");
    }
    bool publish = token_eq(output_kind, "publish"), local = token_eq(output_kind, "local");
    int inputs = object_field(setup, "inputs"), n = 0;
    binding_t candidate[16] = {{0}};
    if (inputs < 0 || setup_tokens[inputs].type != JSMN_ARRAY) {
        return setup_error("inputs_not_array");
    }
    int previous_input = -1;
    for (int t = inputs + 1; t < setup_ntokens && setup_tokens[t].start < setup_tokens[inputs].end; t++) {
        if (setup_tokens[t].parent != inputs) continue;
        if (!separator(previous_input < 0 ? setup_tokens[inputs].start + 1 : token_raw_end(previous_input),
                       token_raw_start(t), previous_input < 0 ? 0 : ','))
            return setup_error("invalid_input_array");
        const char *why = "invalid_or_unavailable_local_input";
        if (n >= 16 || !parse_binding(t, n, &candidate[n], &why)) {
            return setup_error(why);
        }
        previous_input = t;
        n++;
    }
    if (!separator(previous_input < 0 ? setup_tokens[inputs].start + 1 : token_raw_end(previous_input),
                   setup_tokens[inputs].end - 1, 0)) return setup_error("invalid_input_array");
    const model_t *model = &models[model_index];
    /* A bundled model's shape is known before loading; an uploaded one's is
       checked once loaded, and the previous model is restored if it does not fit. */
    if (model->kind != UPLOADED && !shape_ok(model, n, candidate)) {
        return setup_error("input_shape_mismatch");
    }
    int old = active;
    bool was_enabled = enabled;
    bool reload = model_index != old || model->kind == UPLOADED || revision != inference_mgr_revision();
    if (reload) {
        bool loaded = load_model(model);
        if (!loaded || !shape_ok(model, n, candidate)) {
            bool restored = old >= 0 && load_model(&models[old]);
            if (restored) { revision = inference_mgr_revision(); enabled = was_enabled; }
            else { active = -1; enabled = false; }
            if (loaded) return setup_error(restored ? "input_shape_mismatch" : "input_shape_mismatch_restore_failed");
            return setup_error(restored ? "model_load_failed_restored" : "model_load_failed_restore_failed");
        }
    }
    active = model_index;
    revision = inference_mgr_revision();
    memcpy(bindings, candidate, sizeof bindings);
    publish_output = publish;
    local_output = local;
    threshold_enabled = false;
    enabled = true;
    sensor_ml_invalidate();
    char label[48];
    printf("ML SETUP: OK model=%s generation=%lu output=%s inputs=%d source=%s\n", id,
           (unsigned long)generation, output_name(), n, input_label(n, label, sizeof label));
    mqtt_pub_ml_status(true, "applied", (unsigned long)generation);
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
    printf("ML: model=%s enabled=%u generation=%lu output=%s interval_ms=%lu max_age_ms=%lu max_skew_ms=%lu\n",
        active<0 ? "none":models[active].id,enabled,(unsigned long)generation,
        output_name(),
        (unsigned long)interval_ms,(unsigned long)max_age_ms,(unsigned long)max_skew_ms);
    if (threshold_enabled) printf("ML: threshold=%.9g (scalar output)\n", (double)output_threshold);
    else puts("ML: threshold=off");
    for(int i=0;i<16;i++) if(bindings[i].bound) {
        const app_cfg_t *c = scheduler_config_by_name(bindings[i].source);
        bool remote = c && c->proto == proto_mqtt;
        printf("ML: bind=%d source=%s field=%s min=%.6g max=%.6g valid=%u%s%s%s%s\n",i,
            bindings[i].source,bindings[i].field,(double)bindings[i].lo,(double)bindings[i].hi,bindings[i].valid,
            remote ? " remote=" : "", remote ? c->remote_node : "", remote ? "/" : "",
            remote ? c->remote_source : "");
    }
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
    char fields[112], line[512];
    int length = snprintf(line, sizeof line,
           "{\"type\":\"ml\",\"timestamp_ms\":%lu,\"generation\":%lu,\"model\":\"%s\","
           "\"input_source\":\"%s\",\"outputs\":[%d]%s,\"invoke_us\":%lu,\"status\":\"OK\"}",
           (unsigned long)now, (unsigned long)generation, model->id, source, winner,
           class_fields(model, winner, confidence, fields, sizeof fields), (unsigned long)elapsed);
    if (length <= 0 || length >= (int)sizeof line) return false;
    puts(line);
    if (publish_output) {
        if (!mqtt_pub_ml_result(line)) puts("ML: WARN result publish failed");
        if (model->kind == COMFORT && strcmp(source, "replay") != 0 &&
            !mqtt_pub_d6_comfort((unsigned)winner))
            puts("ML: WARN D6 comfort publish failed");
    }
    if (local_output) {
        float index = (float)winner;
        emit_local(&index, 1, true);
    }
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
    if (publish_output) {
        char payload[512];
        int n = snprintf(payload, sizeof payload,
            "{\"type\":\"ml\",\"timestamp_ms\":%lu,\"generation\":%lu,\"model\":\"%s\",\"input_source\":\"%s\",\"outputs\":[",
            (unsigned long)now, (unsigned long)generation, models[active].id, source);
        if (n > 0 && n < (int)sizeof payload) {
            for (int i = 0; i < out_count && n < (int)sizeof payload; i++)
                n += snprintf(payload + n, sizeof payload - (size_t)n,
                              "%s%.7g", i ? "," : "", (double)outputs[i]);
            if (n < (int)sizeof payload)
                n += snprintf(payload + n, sizeof payload - (size_t)n,
                              "],\"invoke_us\":%lu,\"status\":\"OK\"}",
                              (unsigned long)elapsed);
        }
        if (n <= 0 || n >= (int)sizeof payload || !mqtt_pub_ml_result(payload))
            puts("ML: WARN result publish failed");
    }
    if (local_output) emit_local(outputs, out_count, false);
    return true;
}

static bool replay(const char *values) {
    if (active < 0 || revision != inference_mgr_revision() || !inference_mgr_is_ready()) return false;
    const model_t *model = &models[active];
    float inputs[31];
    int count = model_inputs(model);
    if (count < 1 || count > 31) return false;
    for (int i = 0; i < count; i++) {
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
    return scalar_or_vector_result(inputs, count, 0, "replay", now);
}

bool sensor_ml_command(const char *line,bool sampling) {
    if(strncmp(line,"ML",2) || (line[2] && line[2]!=' ')) return false;
    if(!strcmp(line,"ML") || !strcmp(line,"ML HELP")) {
        puts("ML: HELP | SHOW | LIST | PRESET <id> | MODEL <id|uploaded> | REPLAY <model-ready values> | RESET | BIND <index> <source> <field> <min> <max> | UNBIND <index> | TIMING <interval_ms> <age_ms> <skew_ms> | THRESHOLD <value|OFF> | OUTPUT <console|publish|local> | RUN | STOP");
        return true;
    }
    if (!strcmp(line, "ML LIST")) {
        for (size_t m = 0; m < sizeof models / sizeof models[0]; m++) {
            uint32_t size;
            if (models[m].kind == UPLOADED)
                printf("ML: uploaded %s\n", model_bytes(&models[m], &size) ?
                       "available (sent over MQTT; its shape is read when selected)" : "none received over MQTT");
            else
                printf("ML: %s inputs=%d outputs=%d %s\n", models[m].id, models[m].count,
                       models[m].outputs, models[m].bytes ? "available" : "local export required");
        }
        return true;
    }
    /* Where results go can change while sampling, like the threshold. */
    char kind[12], tail;
    if (sscanf(line, "ML OUTPUT %11s %c", kind, &tail) == 1) {
        if (!strcmp(kind, "console") || !strcmp(kind, "publish") || !strcmp(kind, "local")) {
            publish_output = !strcmp(kind, "publish");
            local_output = !strcmp(kind, "local");
            printf("ML: OK OUTPUT %s\n", kind);
        } else puts("ML: ERROR expected ML OUTPUT console, publish or local");
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
        memset(bindings,0,sizeof bindings); enabled=false; publish_output=false; local_output=false; sensor_ml_invalidate();
    } else if(sscanf(line,"ML MODEL %31s %c",id,&extra)==1 || !strcmp(line,"ML PRESET") ||
              sscanf(line,"ML PRESET %31s %c",id,&extra)==1) {
        bool preset=!strncmp(line,"ML PRESET",9);
        if(!strcmp(line,"ML PRESET")) strcpy(id,"sine-float");
        int m;
        const int model_count = sizeof models / sizeof models[0];
        for (m = 0; m < model_count; m++) if (!strcmp(id, models[m].id)) break;
        if (m == model_count) goto invalid;
        const model_t *model = &models[m];
        if (model->kind == UPLOADED && preset) goto invalid;
        if (!model_available(model)) {
            puts(model->kind == UPLOADED ? "ML: ERROR no model has been uploaded over MQTT" :
                 "ML: ERROR people-counter local model export is missing from this build");
            return true;
        }
        if (preset && model->kind != COMFORT && model->kind != DIRECTION &&
            (!field_supported("AHT","temperature_c") || !field_supported("AHT","humidity_pct") ||
             !field_supported("LIGHT","light_lux") || !field_supported("DISTANCE","distance_cm"))) goto invalid;
        if(!select_model(m)) { puts("ML: ERROR model load failed; pipeline disabled"); return true; }
        if (model->kind == UPLOADED)
            printf("ML: uploaded model inputs=%d outputs=%d\n", inference_mgr_input_count(), inference_mgr_output_count());
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
        if (active < 0 || !shape_ok(&models[active], n, bindings) ||
            revision != inference_mgr_revision() || !inference_mgr_is_ready()) goto invalid;
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
    char label[48];
    const char *source = input_label(n, label, sizeof label);
    if (model->kind == COMFORT) {
        class_result(model, inputs, source, now);
        return;
    }
    int count = model_inputs(model);
    if (count == 1 && n > 1) { float sum=0; for(int i=0;i<n;i++) sum+=inputs[i]; inputs[0]=sum/n; }
    if (model->kind == SINE) inputs[0] *= 6.28318530718f;
    scalar_or_vector_result(inputs, count, n, source, now);
}
