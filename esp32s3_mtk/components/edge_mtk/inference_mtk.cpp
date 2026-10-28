// inference_mtk.cpp
// TFLite Micro on the ESP32-S3 with µT-Kernel (D1 task 31): InferenceTask and
// the tflite_wrapper.h API the shared inference manager calls.
//
// InferenceTask is the only task that touches the interpreter, the op
// resolver and the tensor arena (MTK_REDESIGN.md section 5). The shared
// inference manager runs in SchedulerTask, normalises the inputs and scores
// the outputs as before; its tflite_* calls are turned into requests: the
// request (model pointer, or a complete input vector) goes to InferenceTask
// through infer_mbf, and the result (status, outputs, model metadata) comes
// back through a one-message reply buffer while the caller sleeps on it. A
// µT-Kernel mutex keeps requests from different tasks from overlapping.
// SchedulerTask therefore still sees inference complete before its rule
// action continues, so the rule semantics are unchanged.
//
// The engine below is the Arduino build's wrapper (libraries/EdgeS3/src/
// tflite_wrapper_s3.cpp) with the same op set, checks and messages, on the
// same Espressif TFLite Micro release (espressif/esp-tflite-micro 1.4.0, the
// version Arduino-ESP32 3.3.12 bundled). The 64 KB arena is allocated from
// PSRAM, as in the Arduino build.
// µT-Kernel's tk/typedef.h declares types named _B, _H, _W..., which the C
// library's <ctype.h> defines as macros; include it before any C++ header.
extern "C" {
#include <tk/tkernel.h>
#include "inference_engine/tflite_wrapper.h"
#include "runtime_mtk.h"
}

#include <cstdio>
#include <cstring>
#include <new>

#include "esp_heap_caps.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "flatbuffers/flatbuffers.h"

// ---------------- engine (InferenceTask only) ----------------

namespace {

constexpr int kOpCount = 30;

uint8_t *s_arena = nullptr;
const tflite::Model *s_model = nullptr;
tflite::MicroMutableOpResolver<kOpCount> *s_resolver = nullptr;
tflite::MicroInterpreter *s_interpreter = nullptr;
alignas(16) uint8_t s_resolver_buf[sizeof(tflite::MicroMutableOpResolver<kOpCount>)];
alignas(16) uint8_t s_interp_buf[sizeof(tflite::MicroInterpreter)];
tflite_model_info_t s_info;
bool s_loaded = false;

bool arena_alloc()
{
    if (!s_arena) s_arena = (uint8_t *)heap_caps_aligned_alloc(16, TFLITE_ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_arena) s_arena = (uint8_t *)heap_caps_aligned_alloc(16, TFLITE_ARENA_SIZE, MALLOC_CAP_8BIT);
    return s_arena != nullptr;
}

bool register_ops(tflite::MicroMutableOpResolver<kOpCount> &r)
{
    return r.AddFullyConnected() == kTfLiteOk && r.AddAdd() == kTfLiteOk && r.AddMul() == kTfLiteOk &&
           r.AddSub() == kTfLiteOk && r.AddConv2D() == kTfLiteOk && r.AddDepthwiseConv2D() == kTfLiteOk &&
           r.AddAveragePool2D() == kTfLiteOk && r.AddMaxPool2D() == kTfLiteOk && r.AddRelu() == kTfLiteOk &&
           r.AddRelu6() == kTfLiteOk && r.AddSoftmax() == kTfLiteOk && r.AddLogistic() == kTfLiteOk &&
           r.AddTanh() == kTfLiteOk && r.AddLeakyRelu() == kTfLiteOk && r.AddReshape() == kTfLiteOk &&
           r.AddConcatenation() == kTfLiteOk && r.AddPad() == kTfLiteOk && r.AddPack() == kTfLiteOk &&
           r.AddUnpack() == kTfLiteOk && r.AddSplit() == kTfLiteOk && r.AddQuantize() == kTfLiteOk &&
           r.AddDequantize() == kTfLiteOk && r.AddMean() == kTfLiteOk && r.AddReduceMax() == kTfLiteOk &&
           r.AddMaximum() == kTfLiteOk && r.AddMinimum() == kTfLiteOk && r.AddNeg() == kTfLiteOk &&
           r.AddAbs() == kTfLiteOk && r.AddStridedSlice() == kTfLiteOk &&
           r.AddUnidirectionalSequenceLSTM() == kTfLiteOk;
}

void engine_unload()
{
    printf("[TFLITE] Unloading previous model state...\n");
    // TFLite Micro objects live in placement-new storage and are not
    // destroyed: zero the storage and the arena for the next model.
    if (s_interpreter) { memset(s_interp_buf, 0, sizeof s_interp_buf); s_interpreter = nullptr; }
    if (s_resolver) { memset(s_resolver_buf, 0, sizeof s_resolver_buf); s_resolver = nullptr; }
    if (s_arena) memset(s_arena, 0, TFLITE_ARENA_SIZE);
    s_model = nullptr;
    s_loaded = false;
    memset(&s_info, 0, sizeof s_info);
    printf("[TFLITE] Unload complete.\n");
}

// Total element count of a tensor list, bounded by the shared API's limit.
bool count_elements(bool inputs, int &total, int limit)
{
    total = 0;
    int n = inputs ? (int)s_interpreter->inputs_size() : (int)s_interpreter->outputs_size();
    for (int i = 0; i < n; i++) {
        TfLiteTensor *t = inputs ? s_interpreter->input(i) : s_interpreter->output(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) {
            int dim = t->dims->data[d];
            if (dim <= 0 || count > (limit - total) / dim) return false;
            count *= dim;
        }
        if (count > limit - total) return false;
        total += count;
    }
    return true;
}

tflite_err_t engine_load(const uint8_t *data, uint32_t size)
{
    engine_unload();
    if (!data || size == 0) { printf("[TFLITE] No model data\n"); return TFLITE_ERR_NO_MODEL; }
    if (size < 8 || memcmp(data + 4, "TFL3", 4) != 0) { printf("[TFLITE] Invalid model header\n"); return TFLITE_ERR_BAD_MODEL; }
    flatbuffers::Verifier verifier(data, size);
    if (!tflite::VerifyModelBuffer(verifier)) { printf("[TFLITE] Invalid or truncated FlatBuffer\n"); return TFLITE_ERR_BAD_MODEL; }
    static bool s_target_initialized = false;
    if (!s_target_initialized) { tflite::InitializeTarget(); s_target_initialized = true; }

    printf("[TFLITE] Verifying FlatBuffer at %p...\n", data);
    s_model = tflite::GetModel(data);
    if (!s_model) { printf("[TFLITE] FATAL: GetModel returned null!\n"); return TFLITE_ERR_BAD_MODEL; }
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        printf("[TFLITE] Model schema version %lu != expected %d\n", (unsigned long)s_model->version(), TFLITE_SCHEMA_VERSION);
        s_model = nullptr;
        return TFLITE_ERR_BAD_MODEL;
    }
    printf("[TFLITE] Schema version %lu verified.\n", (unsigned long)s_model->version());
    if (!arena_alloc()) { printf("[TFLITE] arena alloc failed\n"); return TFLITE_ERR_ALLOC; }
    printf("[TFLITE] Creating OpResolver...\n");
    s_resolver = new (s_resolver_buf) tflite::MicroMutableOpResolver<kOpCount>();
    if (!register_ops(*s_resolver)) { printf("[TFLITE] Failed to register ops\n"); s_model = nullptr; return TFLITE_ERR_ALLOC; }
    printf("[TFLITE] Ops registered.\n");
    printf("[TFLITE] Creating Interpreter (Arena size %d)...\n", TFLITE_ARENA_SIZE);
    s_interpreter = new (s_interp_buf) tflite::MicroInterpreter(s_model, *s_resolver, s_arena, TFLITE_ARENA_SIZE);
    printf("[TFLITE] Allocating tensors...\n");
    TfLiteStatus status = s_interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        printf("[TFLITE] AllocateTensors() failed with status %d\n", (int)status);
        s_interpreter = nullptr;
        s_model = nullptr;
        return TFLITE_ERR_ALLOC;
    }
    printf("[TFLITE] Tensors allocated.\n");
    s_info.num_inputs = (int)s_interpreter->inputs_size();
    s_info.num_outputs = (int)s_interpreter->outputs_size();
    if (!count_elements(true, s_info.input_size, TFLITE_MAX_INPUTS)) {
        printf("[TFLITE] Input exceeds shared API capacity\n");
        engine_unload();
        return TFLITE_ERR_BAD_MODEL;
    }
    if (!count_elements(false, s_info.output_size, TFLITE_MAX_OUTPUTS)) {
        printf("[TFLITE] Output exceeds shared API capacity\n");
        engine_unload();
        return TFLITE_ERR_BAD_MODEL;
    }
    s_loaded = true;
    printf("[TFLITE] Model loaded OK!\n");
    printf("[TFLITE]   Inputs:  %d tensor(s), %d total element(s)\n", s_info.num_inputs, s_info.input_size);
    printf("[TFLITE]   Outputs: %d tensor(s), %d total element(s)\n", s_info.num_outputs, s_info.output_size);
    printf("[TFLITE]   Arena:   %u / %d bytes used\n", (unsigned)s_interpreter->arena_used_bytes(), TFLITE_ARENA_SIZE);
    return TFLITE_OK;
}

tflite_err_t engine_invoke(const float *inputs, float *outputs)
{
    if (!s_loaded || !s_interpreter) return TFLITE_ERR_NOT_LOADED;
    int offset = 0;
    for (int i = 0; i < s_info.num_inputs; i++) {
        TfLiteTensor *t = s_interpreter->input(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) count *= t->dims->data[d];
        if (t->type == kTfLiteFloat32) {
            memcpy(t->data.f, inputs + offset, count * sizeof(float));
        } else if (t->type == kTfLiteInt8) {
            float scale = t->params.scale;
            int zero_point = t->params.zero_point;
            for (int k = 0; k < count; k++) {
                float val = inputs[offset + k];
                int32_t q = (int32_t)(val / scale + (val >= 0 ? 0.5f : -0.5f)) + zero_point;
                if (q < -128) q = -128;
                if (q > 127) q = 127;
                t->data.int8[k] = (int8_t)q;
            }
        } else {
            printf("[TFLITE] Unsupported input tensor type %d\n", t->type);
            return TFLITE_ERR_INVOKE;
        }
        offset += count;
    }
    if (s_interpreter->Invoke() != kTfLiteOk) { printf("[TFLITE] Invoke() failed\n"); return TFLITE_ERR_INVOKE; }
    offset = 0;
    for (int i = 0; i < s_info.num_outputs; i++) {
        TfLiteTensor *t = s_interpreter->output(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) count *= t->dims->data[d];
        if (t->type == kTfLiteFloat32) {
            memcpy(outputs + offset, t->data.f, count * sizeof(float));
        } else if (t->type == kTfLiteInt8) {
            float scale = t->params.scale;
            int zero_point = t->params.zero_point;
            for (int k = 0; k < count; k++) outputs[offset + k] = (t->data.int8[k] - zero_point) * scale;
        } else {
            printf("[TFLITE] Unsupported output tensor type %d\n", t->type);
            return TFLITE_ERR_INVOKE;
        }
        offset += count;
    }
    return TFLITE_OK;
}

// ---------------- requests between SchedulerTask and InferenceTask ----------------

enum { REQ_LOAD = 1, REQ_INVOKE, REQ_UNLOAD };

// Each request carries a sequence number that its reply echoes, so a reply
// that arrives after its caller gave up is recognised and discarded.
struct infer_req_t {
    int32_t        op;
    uint32_t       seq;
    const uint8_t *model;
    uint32_t       size;
    float          in[TFLITE_MAX_INPUTS];
};

struct infer_rep_t {
    int32_t             err;
    uint32_t            seq;
    bool                loaded;
    tflite_model_info_t info;
    float               out[TFLITE_MAX_OUTPUTS];
};

static_assert(sizeof(infer_req_t) <= INFER_MSG_MAX, "infer_mbf messages are too small");

ID s_rep_mbf, s_rpc_mtx;
uint32_t s_rep_buf[2 * (4 + ((sizeof(infer_rep_t) + 3) & ~3u)) / 4];
tflite_model_info_t s_info_cache;     // the caller's copy of the metadata
volatile bool s_ready_cache;
uint32_t s_seq;                       // guarded by s_rpc_mtx

// Sends one request and sleeps until its reply arrives (at most 5 s).
tflite_err_t request(infer_req_t &req, infer_rep_t &rep)
{
    if (s_rpc_mtx <= 0 || tk_loc_mtx(s_rpc_mtx, 5000) != E_OK) return TFLITE_ERR_INVOKE;
    req.seq = ++s_seq;
    bool answered = false;
    if (tk_snd_mbf(infer_mbf, &req, sizeof req, 1000) == E_OK) {
        while (tk_rcv_mbf(s_rep_mbf, &rep, 5000) == (INT)sizeof rep) {
            if (rep.seq == req.seq) { answered = true; break; }
            // otherwise: the late reply to an earlier request; discard it
        }
    }
    tflite_err_t err = TFLITE_ERR_INVOKE;
    if (answered) {
        err = (tflite_err_t)rep.err;
        s_info_cache = rep.info;
        s_ready_cache = rep.loaded;
    } else {
        printf("[INFER] InferenceTask did not answer\n");
    }
    tk_unl_mtx(s_rpc_mtx);
    return err;
}

}  // namespace

// ---------------- InferenceTask and the C API ----------------

extern "C" ER inference_mtk_init(void)
{
    T_CMBF cmbf = {};
    cmbf.mbfatr = TA_TFIFO | TA_USERBUF;
    cmbf.bufsz = sizeof s_rep_buf;
    cmbf.maxmsz = sizeof(infer_rep_t);
    cmbf.bufptr = s_rep_buf;
    ID mbf = tk_cre_mbf(&cmbf);
    if (mbf < E_OK) return (ER)mbf;
    T_CMTX cmtx = {};
    cmtx.mtxatr = TA_TFIFO | TA_INHERIT;
    ID mtx = tk_cre_mtx(&cmtx);
    if (mtx < E_OK) return (ER)mtx;
    s_rep_mbf = mbf;
    s_rpc_mtx = mtx;
    return E_OK;
}

extern "C" void inference_task_body(void)
{
    static infer_req_t req;
    static infer_rep_t rep;
    for (;;) {
        if (tk_rcv_mbf(infer_mbf, &req, TMO_FEVR) != (INT)sizeof req) continue;
        runtime_alive(RT_INFERENCE);
        memset(&rep, 0, sizeof rep);
        switch (req.op) {
        case REQ_LOAD:   rep.err = engine_load(req.model, req.size); break;
        case REQ_INVOKE: rep.err = engine_invoke(req.in, rep.out); break;
        case REQ_UNLOAD: engine_unload(); rep.err = TFLITE_OK; break;
        default:         rep.err = TFLITE_ERR_INVOKE; break;
        }
        rep.seq = req.seq;
        rep.loaded = s_loaded;
        rep.info = s_info;
        tk_snd_mbf(s_rep_mbf, &rep, sizeof rep, 1000);
    }
}

// The callers' request and reply are on their own stacks (under 100 bytes
// each), so callers in different tasks never share a buffer.
extern "C" tflite_err_t tflite_load_model(const uint8_t *model_data, uint32_t model_size)
{
    infer_req_t req = {};
    infer_rep_t rep;
    req.op = REQ_LOAD;
    req.model = model_data;
    req.size = model_size;
    return request(req, rep);
}

extern "C" tflite_err_t tflite_invoke(const float *inputs, float *outputs)
{
    if (!s_ready_cache) return TFLITE_ERR_NOT_LOADED;
    infer_req_t req = {};
    infer_rep_t rep;
    int nin = s_info_cache.input_size, nout = s_info_cache.output_size;
    req.op = REQ_INVOKE;
    memcpy(req.in, inputs, (size_t)nin * sizeof(float));
    tflite_err_t err = request(req, rep);
    if (err == TFLITE_OK) memcpy(outputs, rep.out, (size_t)nout * sizeof(float));
    return err;
}

extern "C" const tflite_model_info_t *tflite_get_info(void)
{
    return s_ready_cache ? &s_info_cache : nullptr;
}

extern "C" void tflite_unload_model(void)
{
    infer_req_t req = {};
    infer_rep_t rep;
    req.op = REQ_UNLOAD;
    (void)request(req, rep);
}

extern "C" bool tflite_is_ready(void)
{
    return s_ready_cache;
}
