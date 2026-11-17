// ml_bench.c - D5 tasks 4.2 to 4.4: time the loaded model on the board.
//
// MLBENCH <runs> [RAW] runs the model that ML MODEL or ML PRESET loaded,
// first five times as a warm-up and then <runs> times (10 to 1000), on one
// fixed input vector (every input 0.5). It runs in SchedulerTask, the task
// that runs the ML pipeline, and only while sampling is stopped, so nothing
// else uses the model meanwhile. For each run it records two times:
//
//   request_us  inference_mgr_set_inputs() and inference_mgr_run() as seen
//               here: the same interval sensor_ml.c reports as invoke_us,
//               including the infer_mbf round trip to InferenceTask
//   invoke_us   Invoke() alone inside InferenceTask (inference_mtk.cpp)
//
// and their difference, overhead_us. Nothing is printed inside the timed
// loop; afterwards it prints, for each of the three, the median, 95th
// percentile, minimum, maximum and mean, then one JSON line for
// tools/d5_bench.py, then the free internal RAM and PSRAM. RAW also prints
// every run first. The loaded model, its bindings and the ML pipeline are
// left as they were.
#ifdef ESP_PLATFORM
#include <tk/tkernel.h>
#include "esp_heap_caps.h"
#include "runtime_mtk.h"
#endif
#include "ml_bench.h"
#include "hal/hal.h"
#include "inference_engine/inference_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef ESP_PLATFORM
// Up to 12 KB of samples: from PSRAM, where the tensor arena also lives,
// so internal RAM is not used.
static void *bench_alloc(size_t bytes)
{
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}
#define BENCH_FREE(p) heap_caps_free(p)
// A long run must not look like a stuck SchedulerTask to MonitorTask.
#define BENCH_ALIVE() runtime_alive(RT_SCHEDULER)
#else
#define bench_alloc(bytes) malloc(bytes)
#define BENCH_FREE(p) free(p)
#define BENCH_ALIVE() ((void)0)
#endif

#define BENCH_WARMUP   5
#define BENCH_MIN_RUNS 10
#define BENCH_MAX_RUNS 1000
#define BENCH_INPUT    0.5f

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void ml_bench_stats(uint32_t *v, int n, ml_bench_stat_t *out)
{
    qsort(v, (size_t)n, sizeof v[0], cmp_u32);
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];
    out->median = n % 2 ? v[n / 2] : (uint32_t)(((uint64_t)v[n / 2 - 1] + v[n / 2]) / 2);
    out->p95 = v[(95 * n + 99) / 100 - 1];
    out->min = v[0];
    out->max = v[n - 1];
    out->mean = (uint32_t)((sum + (uint64_t)n / 2) / (uint64_t)n);
}

static void print_stat(const char *name, const ml_bench_stat_t *s)
{
    printf("[MLBENCH] %-11s median=%lu p95=%lu min=%lu max=%lu mean=%lu\n", name,
           (unsigned long)s->median, (unsigned long)s->p95, (unsigned long)s->min,
           (unsigned long)s->max, (unsigned long)s->mean);
}

static int json_stat(char *buf, size_t size, const char *name, const ml_bench_stat_t *s)
{
    return snprintf(buf, size, ",\"%s\":{\"median\":%lu,\"p95\":%lu,\"min\":%lu,\"max\":%lu,\"mean\":%lu}",
                    name, (unsigned long)s->median, (unsigned long)s->p95, (unsigned long)s->min,
                    (unsigned long)s->max, (unsigned long)s->mean);
}

// One timed inference; false if the model refused the inputs or failed.
static bool timed_run(const float *inputs, int count, uint32_t *request_us, uint32_t *invoke_us)
{
    uint64_t start = hal_time_us();
    bool ok = inference_mgr_set_inputs(inputs, count) && inference_mgr_run();
    uint64_t end = hal_time_us();
    *request_us = (uint32_t)(end - start);
    *invoke_us = inference_mtk_last_invoke_us();
    return ok;
}

static void usage(void)
{
    puts("[MLBENCH] usage: MLBENCH <runs 10..1000> [RAW] (after STOP and ML MODEL <id>)");
}

bool ml_bench_command(const char *line, bool sampling)
{
    if (strncasecmp(line, "MLBENCH", 7) || (line[7] && line[7] != ' ')) return false;

    char *end;
    const char *p = line + 7;
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') { usage(); return true; }
    unsigned long runs = strtoul(p, &end, 10);
    p = end;
    while (*p == ' ') p++;
    bool raw = false;
    if (!strncasecmp(p, "RAW", 3) && (p[3] == 0 || p[3] == ' ')) {
        raw = true;
        p += 3;
        while (*p == ' ') p++;
    }
    if (*p || runs < BENCH_MIN_RUNS || runs > BENCH_MAX_RUNS) { usage(); return true; }
    if (sampling) { puts("[MLBENCH] ERROR send STOP first"); return true; }

    int inputs = inference_mgr_input_count(), outputs = inference_mgr_output_count();
    if (!inference_mgr_is_ready() || inputs < 1 || inputs > INFER_MAX_INPUTS) {
        puts("[MLBENCH] ERROR no model loaded; send ML MODEL <id> first");
        return true;
    }

    int n = (int)runs;
    uint32_t *request = bench_alloc((size_t)n * sizeof *request);
    uint32_t *invoke = bench_alloc((size_t)n * sizeof *invoke);
    uint32_t *overhead = bench_alloc((size_t)n * sizeof *overhead);
    if (!request || !invoke || !overhead) {
        puts("[MLBENCH] ERROR out of memory for the samples");
        if (request) BENCH_FREE(request);
        if (invoke) BENCH_FREE(invoke);
        if (overhead) BENCH_FREE(overhead);
        return true;
    }

    float in[INFER_MAX_INPUTS];
    for (int i = 0; i < inputs; i++) in[i] = BENCH_INPUT;

    bool ok = true;
    int failed = -1;
    uint32_t req_us, inv_us;
    for (int i = 0; i < BENCH_WARMUP && ok; i++) {
        ok = timed_run(in, inputs, &req_us, &inv_us);
        if (!ok) failed = -1 - i;
        BENCH_ALIVE();
    }
    for (int i = 0; i < n && ok; i++) {
        ok = timed_run(in, inputs, &request[i], &invoke[i]);
        if (!ok) failed = i;
        BENCH_ALIVE();
    }

    if (!ok) {
        if (failed < 0) printf("[MLBENCH] ERROR warm-up run %d failed\n", -failed);
        else printf("[MLBENCH] ERROR run %d failed\n", failed + 1);
    } else {
        float out0 = 0.0f;
        bool have_out = inference_mgr_get_output(0, &out0);
        if (raw)
            for (int i = 0; i < n; i++)
                printf("[MLBENCH] run=%d request_us=%lu invoke_us=%lu\n", i + 1,
                       (unsigned long)request[i], (unsigned long)invoke[i]);
        for (int i = 0; i < n; i++) overhead[i] = request[i] > invoke[i] ? request[i] - invoke[i] : 0;
        ml_bench_stat_t rq, iv, ov;
        ml_bench_stats(request, n, &rq);
        ml_bench_stats(invoke, n, &iv);
        ml_bench_stats(overhead, n, &ov);

        printf("[MLBENCH] runs=%d warmup=%d inputs=%d outputs=%d input=%.1f output0=%.7g\n",
               n, BENCH_WARMUP, inputs, outputs, (double)BENCH_INPUT, have_out ? (double)out0 : 0.0);
        print_stat("request_us", &rq);
        print_stat("invoke_us", &iv);
        print_stat("overhead_us", &ov);

        char json[512];        // at most about 410 characters
        int used = snprintf(json, sizeof json,
                            "{\"type\":\"mlbench\",\"runs\":%d,\"warmup\":%d,\"inputs\":%d,\"outputs\":%d,\"output0\":%.7g",
                            n, BENCH_WARMUP, inputs, outputs, have_out ? (double)out0 : 0.0);
        used += json_stat(json + used, sizeof json - (size_t)used, "request_us", &rq);
        used += json_stat(json + used, sizeof json - (size_t)used, "invoke_us", &iv);
        used += json_stat(json + used, sizeof json - (size_t)used, "overhead_us", &ov);
        snprintf(json + used, sizeof json - (size_t)used, "}");
        puts(json);
    }
    BENCH_FREE(request);
    BENCH_FREE(invoke);
    BENCH_FREE(overhead);
#ifdef ESP_PLATFORM
    // D5 task 4.3: the memory left with this model loaded and Wi-Fi and
    // MQTT still running (STOP only stops sampling), after the samples are freed.
    if (ok)
        printf("[MLBENCH] memory internal_free=%u B internal_min_free=%u B psram_free=%u B\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
    return true;
}
