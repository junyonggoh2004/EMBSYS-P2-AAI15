// Diagnostic sketch for Task 20: test the checked-in FP32 SVDD model.
#include <EdgeS3.h>
#include "inference_engine/tflite_wrapper.h"
#include "svdd_model_data.h"
#include "tensorflow/lite/schema/schema_generated.h"

static void print_shape(const char *label, const tflite::SubGraph *graph,
                        const flatbuffers::Vector<int32_t> *indices) {
    for (unsigned i = 0; i < indices->size(); ++i) {
        const auto *tensor = graph->tensors()->Get(indices->Get(i));
        printf("[SVDD] %s[%u] type=%d shape=", label, i, (int)tensor->type());
        const auto *shape = tensor->shape();
        for (unsigned d = 0; d < shape->size(); ++d)
            printf("%s%ld", d ? "x" : "", (long)shape->Get(d));
        printf("\n");
    }
}

void setup() {
    Serial.begin(115200);
    delay(2500);
    printf("[SVDD] probe start, model=%u bytes\n", (unsigned)sizeof(kSvddModel));
    const auto *model = tflite::GetModel(kSvddModel);
    const auto *graph = model->subgraphs()->Get(0);
    print_shape("input", graph, graph->inputs());
    print_shape("output", graph, graph->outputs());
    tflite_err_t err = tflite_load_model(kSvddModel, sizeof(kSvddModel));
    printf("[SVDD] load=%d\n", (int)err);
    if (err != TFLITE_OK) return;
    const tflite_model_info_t *info = tflite_get_info();
    printf("[SVDD] inputs=%d outputs=%d\n", info->input_size, info->output_size);
    float inputs[TFLITE_MAX_INPUTS] = {};
    float outputs[TFLITE_MAX_OUTPUTS] = {};
    err = tflite_invoke(inputs, outputs);
    printf("[SVDD] invoke=%d\n", (int)err);
    if (err == TFLITE_OK) {
        for (int i = 0; i < info->output_size; ++i)
            printf("[SVDD] out[%d]=%.6f\n", i, outputs[i]);
    }
}

void loop() { delay(1000); }
