/* D5 tasks 4.2 to 4.4: the MLBENCH command and its statistics, with the
 * model, the clock and InferenceTask's Invoke() time simulated. The real
 * ml_bench.c is compiled. */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "inference_engine/inference_manager.h"
#include "ml_bench.h"

/* ---- simulated model, clock and InferenceTask ---- */
static bool ready = true;
static int n_inputs = 1, n_outputs = 1;
static int runs, fail_at = -1, last_count;
static float last_input;
static uint64_t now_us;
static uint32_t invoke_us, request_extra_us = 300;

uint64_t hal_time_us(void) { return now_us; }
bool inference_mgr_is_ready(void) { return ready; }
int inference_mgr_input_count(void) { return n_inputs; }
int inference_mgr_output_count(void) { return n_outputs; }
bool inference_mgr_set_inputs(const float *in, int count)
{
    last_count = count;
    last_input = in[count - 1];
    return count == n_inputs;
}
/* Each run's Invoke() takes 100 + (run % 10) µs; the request adds a fixed overhead. */
bool inference_mgr_run(void)
{
    if (runs++ == fail_at) return false;
    invoke_us = 100 + (uint32_t)(runs % 10);
    now_us += invoke_us + request_extra_us;
    return true;
}
bool inference_mgr_get_output(int index, float *out) { (void)index; *out = 0.25f; return true; }
uint32_t inference_mtk_last_invoke_us(void) { return invoke_us; }

/* ---- output capture ---- */
static char out_text[200000];
static size_t out_len;
static int capture_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out_text + out_len, sizeof out_text - out_len, fmt, ap);
    va_end(ap);
    if (n > 0) out_len += (size_t)n;
    return n;
}
static int capture_puts(const char *s) { return capture_printf("%s\n", s); }
#define printf capture_printf
#define puts capture_puts
#include "../esp32s3_mtk/components/edge_mtk/ml_bench.c"
#undef printf
#undef puts

static void reset(void) { out_len = 0; out_text[0] = 0; runs = 0; fail_at = -1; }
static bool has(const char *s) { return strstr(out_text, s) != NULL; }
static int count_of(const char *s)
{
    int n = 0;
    for (const char *p = out_text; (p = strstr(p, s)); p += strlen(s)) n++;
    return n;
}

int main(void)
{
    /* Only MLBENCH lines are taken; ML commands and others pass through. */
    assert(!ml_bench_command("ML SHOW", false));
    assert(!ml_bench_command("MLBENCHX 10", false));
    assert(!ml_bench_command("RUN", false));

    /* Usage errors, the STOP rule and a missing model never run the model. */
    const char *bad[] = { "MLBENCH", "MLBENCH 9", "MLBENCH 1001", "MLBENCH ten", "MLBENCH 10 RAWX",
                          "MLBENCH 10 RAW extra", "MLBENCH -5" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        reset();
        assert(ml_bench_command(bad[i], false) && has("usage") && runs == 0);
    }
    reset();
    assert(ml_bench_command("MLBENCH 10", true) && has("ERROR send STOP first") && runs == 0);
    reset();
    ready = false;
    assert(ml_bench_command("MLBENCH 10", false) && has("ERROR no model loaded") && runs == 0);
    ready = true;

    /* 100 runs after 5 warm-ups. Invoke() times are 100..109 µs, ten of
     * each, so the median is 104.5 -> 104 (integer mean of 104 and 105), p95
     * is the 95th of 100 sorted values (109), and the request adds 300 µs. */
    reset();
    assert(ml_bench_command("mlbench 100", false));
    assert(runs == 105 && last_count == 1 && last_input == 0.5f);
    assert(has("[MLBENCH] runs=100 warmup=5 inputs=1 outputs=1 input=0.5 output0=0.25"));
    assert(has("[MLBENCH] invoke_us   median=104 p95=109 min=100 max=109 mean=105"));
    assert(has("[MLBENCH] request_us  median=404 p95=409 min=400 max=409 mean=405"));
    assert(has("[MLBENCH] overhead_us median=300 p95=300 min=300 max=300 mean=300"));
    assert(has("{\"type\":\"mlbench\",\"runs\":100,\"warmup\":5,\"inputs\":1,\"outputs\":1,\"output0\":0.25,"
               "\"request_us\":{\"median\":404,\"p95\":409,\"min\":400,\"max\":409,\"mean\":405},"
               "\"invoke_us\":{\"median\":104,\"p95\":109,\"min\":100,\"max\":109,\"mean\":105},"
               "\"overhead_us\":{\"median\":300,\"p95\":300,\"min\":300,\"max\":300,\"mean\":300}}"));
    assert(count_of("run=") == 0);

    /* RAW prints every run before the summary; a 16-input model gets 16 inputs. */
    reset();
    n_inputs = 16;
    assert(ml_bench_command("MLBENCH 10 raw", false));
    assert(runs == 15 && last_count == 16 && count_of("[MLBENCH] run=") == 10);
    assert(has("[MLBENCH] run=1 request_us=") && has("[MLBENCH] run=10 request_us="));
    assert(strstr(out_text, "run=10 ") < strstr(out_text, "runs=10 warmup=5 inputs=16"));
    n_inputs = 1;

    /* A failed warm-up or timed run is reported and gives no statistics. */
    reset();
    fail_at = 2;
    assert(ml_bench_command("MLBENCH 10", false) && has("ERROR warm-up run 3 failed") && !has("median"));
    reset();
    fail_at = 5 + 7;
    assert(ml_bench_command("MLBENCH 10", false) && has("ERROR run 8 failed") && !has("median"));

    /* Statistics: odd and even counts, nearest-rank p95, rounded mean. */
    uint32_t odd[] = { 5, 1, 3 };
    ml_bench_stat_t s;
    ml_bench_stats(odd, 3, &s);
    assert(s.median == 3 && s.p95 == 5 && s.min == 1 && s.max == 5 && s.mean == 3);
    uint32_t even[] = { 4, 1, 2, 3 };
    ml_bench_stats(even, 4, &s);
    assert(s.median == 2 && s.p95 == 4 && s.min == 1 && s.max == 4 && s.mean == 3);
    uint32_t big[20];
    for (int i = 0; i < 20; i++) big[i] = (uint32_t)(20 - i);
    ml_bench_stats(big, 20, &s);
    assert(s.p95 == 19 && s.median == 10 && s.mean == 11);

    printf("MLBENCH command and statistics: PASS\n");
    return 0;
}
