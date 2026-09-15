/* tflite_wrapper.cpp — C++ bridge between TFLite Micro and the C firmware
 *
 * This file is the ONLY C++ file in the project. It wraps the TFLite Micro
 * interpreter and exposes a pure C API via extern "C" so that the scheduler
 * and rule engine can call it without any C++ knowledge.
 */

#include "inference_engine/tflite_wrapper.h"

#include <cstdio>
#include <cstring>
#include "pico/stdlib.h"

// TFLite Micro headers (from pico-tflmicro submodule)
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* ---- Static storage ---- */

// Number of ops we register.  Must be >= the count of AddXxx() calls below.
static constexpr int kOpCount = 30;

// Tensor arena — allocated in BSS (RAM).  Tune TFLITE_ARENA_SIZE in header.
alignas(16) static uint8_t s_arena[TFLITE_ARENA_SIZE];

// Pointers managed by load/unload
static const tflite::Model*                                  s_model       = nullptr;
static tflite::MicroMutableOpResolver<kOpCount>*             s_resolver    = nullptr;
static tflite::MicroInterpreter*                             s_interpreter = nullptr;

// Persistent storage for the resolver and interpreter objects
alignas(16) static uint8_t s_resolver_buf[sizeof(tflite::MicroMutableOpResolver<kOpCount>)];
alignas(16) static uint8_t s_interp_buf[sizeof(tflite::MicroInterpreter)];

// Metadata cache
static tflite_model_info_t s_info;
static bool s_loaded = false;

/* ---- Register standard TinyML operations ---- */
static bool register_ops(tflite::MicroMutableOpResolver<kOpCount>& resolver) {
    // Core math / dense layers
    if (resolver.AddFullyConnected() != kTfLiteOk) return false;
    if (resolver.AddAdd()            != kTfLiteOk) return false;
    if (resolver.AddMul()            != kTfLiteOk) return false;
    if (resolver.AddSub()            != kTfLiteOk) return false;

    // Convolution
    if (resolver.AddConv2D()         != kTfLiteOk) return false;
    if (resolver.AddDepthwiseConv2D()!= kTfLiteOk) return false;

    // Pooling
    if (resolver.AddAveragePool2D()  != kTfLiteOk) return false;
    if (resolver.AddMaxPool2D()      != kTfLiteOk) return false;

    // Activations
    if (resolver.AddRelu()           != kTfLiteOk) return false;
    if (resolver.AddRelu6()          != kTfLiteOk) return false;
    if (resolver.AddSoftmax()        != kTfLiteOk) return false;
    if (resolver.AddLogistic()       != kTfLiteOk) return false;
    if (resolver.AddTanh()           != kTfLiteOk) return false;
    if (resolver.AddLeakyRelu()      != kTfLiteOk) return false;

    // Shape manipulation
    if (resolver.AddReshape()        != kTfLiteOk) return false;
    if (resolver.AddConcatenation()  != kTfLiteOk) return false;
    if (resolver.AddPad()            != kTfLiteOk) return false;
    if (resolver.AddPack()           != kTfLiteOk) return false;
    if (resolver.AddUnpack()         != kTfLiteOk) return false;
    if (resolver.AddSplit()          != kTfLiteOk) return false;

    // Quantization support
    if (resolver.AddQuantize()       != kTfLiteOk) return false;
    if (resolver.AddDequantize()     != kTfLiteOk) return false;

    // Reduction / statistics
    if (resolver.AddMean()           != kTfLiteOk) return false;
    if (resolver.AddReduceMax()      != kTfLiteOk) return false;

    // Comparison
    if (resolver.AddMaximum()        != kTfLiteOk) return false;
    if (resolver.AddMinimum()        != kTfLiteOk) return false;

    // Misc
    if (resolver.AddNeg()            != kTfLiteOk) return false;
    if (resolver.AddAbs()            != kTfLiteOk) return false;
    if (resolver.AddStridedSlice()   != kTfLiteOk) return false;

    // LSTM (for time-series / anomaly detection)
    if (resolver.AddUnidirectionalSequenceLSTM() != kTfLiteOk) return false;

    return true;
}

/* ---- Extern "C" API implementation ---- */

extern "C" {

tflite_err_t tflite_load_model(const uint8_t *model_data, uint32_t model_size) {
    // Unload any previous model
    tflite_unload_model();

    if (!model_data || model_size == 0) {
        printf("[TFLITE] No model data\n");
        return TFLITE_ERR_NO_MODEL;
    }

    // Initialise TFLite Micro system (sets up logging etc) ONLY ONCE
    static bool s_target_initialized = false;
    if (!s_target_initialized) {
        tflite::InitializeTarget();
        s_target_initialized = true;
    }

    // Verify the FlatBuffer
    printf("[TFLITE] Verifying FlatBuffer at %p...\n", model_data);
    sleep_ms(20);
    s_model = tflite::GetModel(model_data);
    if (!s_model) {
        printf("[TFLITE] FATAL: GetModel returned null!\n");
        return TFLITE_ERR_BAD_MODEL;
    }

    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        printf("[TFLITE] Model schema version %lu != expected %d\n",
               (unsigned long)s_model->version(), TFLITE_SCHEMA_VERSION);
        s_model = nullptr;
        return TFLITE_ERR_BAD_MODEL;
    }
    printf("[TFLITE] Schema version %lu verified.\n", (unsigned long)s_model->version());
    sleep_ms(20);

    // Construct the op resolver in pre-allocated memory
    printf("[TFLITE] Creating OpResolver...\n");
    s_resolver = new (s_resolver_buf)
        tflite::MicroMutableOpResolver<kOpCount>();

    if (!register_ops(*s_resolver)) {
        printf("[TFLITE] Failed to register ops\n");
        s_model = nullptr;
        return TFLITE_ERR_ALLOC;
    }
    printf("[TFLITE] Ops registered.\n");

    // Construct the interpreter in pre-allocated memory
    printf("[TFLITE] Creating Interpreter (Arena size %d)...\n", TFLITE_ARENA_SIZE);
    s_interpreter = new (s_interp_buf)
        tflite::MicroInterpreter(s_model, *s_resolver, s_arena,
                                 TFLITE_ARENA_SIZE);

    // Allocate tensors
    printf("[TFLITE] Allocating tensors...\n");
    TfLiteStatus status = s_interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        printf("[TFLITE] AllocateTensors() failed with status %d\n", (int)status);
        s_interpreter = nullptr;
        s_model = nullptr;
        return TFLITE_ERR_ALLOC;
    }
    printf("[TFLITE] Tensors allocated.\n");

    // Populate metadata
    s_info.num_inputs  = s_interpreter->inputs_size();
    s_info.num_outputs = s_interpreter->outputs_size();

    // Calculate total input/output sizes (flatten all tensors)
    s_info.input_size = 0;
    for (int i = 0; i < s_info.num_inputs; i++) {
        TfLiteTensor* t = s_interpreter->input(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) count *= t->dims->data[d];
        s_info.input_size += count;
    }

    s_info.output_size = 0;
    for (int i = 0; i < s_info.num_outputs; i++) {
        TfLiteTensor* t = s_interpreter->output(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) count *= t->dims->data[d];
        s_info.output_size += count;
    }

    s_loaded = true;

    printf("[TFLITE] Model loaded OK!\n");
    printf("[TFLITE]   Inputs:  %d tensor(s), %d total element(s)\n",
           s_info.num_inputs, s_info.input_size);
    printf("[TFLITE]   Outputs: %d tensor(s), %d total element(s)\n",
           s_info.num_outputs, s_info.output_size);
    printf("[TFLITE]   Arena:   %zu / %d bytes used\n",
           s_interpreter->arena_used_bytes(), TFLITE_ARENA_SIZE);

    return TFLITE_OK;
}

tflite_err_t tflite_invoke(const float *inputs, float *outputs) {
    if (!s_loaded || !s_interpreter) {
        return TFLITE_ERR_NOT_LOADED;
    }

    // Copy inputs into the input tensor(s) with type conversion
    int offset = 0;
    for (int i = 0; i < s_info.num_inputs; i++) {
        TfLiteTensor* t = s_interpreter->input(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) count *= t->dims->data[d];
        
        if (t->type == kTfLiteFloat32) {
            memcpy(t->data.f, inputs + offset, count * sizeof(float));
        } else if (t->type == kTfLiteInt8) {
            float scale = t->params.scale;
            int zero_point = t->params.zero_point;
            
            for (int k = 0; k < count; k++) {
                float val = inputs[offset + k];
                // Use rounding to the nearest integer
                int32_t q = (int32_t)(val / scale + (val >= 0 ? 0.5f : -0.5f)) + zero_point;
                if (q < -128) q = -128; // Manual bounds clamp
                if (q > 127)  q = 127;
                t->data.int8[k] = (int8_t)q;
            }
        } else {
            printf("[TFLITE] Unsupported input tensor type %d\n", t->type);
            return TFLITE_ERR_INVOKE;
        }
        
        offset += count;
    }

    // Run inference
    TfLiteStatus status = s_interpreter->Invoke();
    if (status != kTfLiteOk) {
        printf("[TFLITE] Invoke() failed\n");
        return TFLITE_ERR_INVOKE;
    }

    // Copy outputs from the output tensor(s) with type conversion
    offset = 0;
    for (int i = 0; i < s_info.num_outputs; i++) {
        TfLiteTensor* t = s_interpreter->output(i);
        int count = 1;
        for (int d = 0; d < t->dims->size; d++) count *= t->dims->data[d];
        
        if (t->type == kTfLiteFloat32) {
            memcpy(outputs + offset, t->data.f, count * sizeof(float));
        } else if (t->type == kTfLiteInt8) {
            float scale = t->params.scale;
            int zero_point = t->params.zero_point;
            
            for (int k = 0; k < count; k++) {
                outputs[offset + k] = (t->data.int8[k] - zero_point) * scale;
            }
        } else {
            printf("[TFLITE] Unsupported output tensor type %d\n", t->type);
            return TFLITE_ERR_INVOKE;
        }
        
        offset += count;
    }

    return TFLITE_OK;
}

const tflite_model_info_t *tflite_get_info(void) {
    return s_loaded ? &s_info : nullptr;
}

void tflite_unload_model(void) {
    printf("[TFLITE] Unloading previous model state...\n");
    sleep_ms(20);

    /* TFLite Micro objects are designed to be statically allocated or use placement new 
     * without relying on standard C++ destructors. Calling ~MicroMutableOpResolver() 
     * or ~MicroInterpreter() can cause hard faults because they are built without RTTI/Exceptions 
     * and sometimes strip virtual destructors to save flash.
     * We simply zero the memory to ensure a clean state for the next placement new. */

    if (s_interpreter) {
        memset(s_interp_buf, 0, sizeof(s_interp_buf));
        s_interpreter = nullptr;
    }
    
    if (s_resolver) {
        memset(s_resolver_buf, 0, sizeof(s_resolver_buf));
        s_resolver = nullptr;
    }

    /* We must also zero the arena to prevent the new interpreter from using 
     * stale metadata or dimension pointers from the previous model. */
    memset(s_arena, 0, sizeof(s_arena));

    s_model  = nullptr;
    s_loaded = false;
    memset(&s_info, 0, sizeof(s_info));
    
    printf("[TFLITE] Unload complete.\n");
    sleep_ms(20);
}

bool tflite_is_ready(void) {
    return s_loaded;
}

}  // extern "C"
