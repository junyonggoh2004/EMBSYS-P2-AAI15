#pragma once
// D5 tasks 4.2 to 4.4: time the loaded model on the board.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// MLBENCH <runs> [RAW], typed on the console or sent on pico/<node>/cmd.
// Returns false when the line is not an MLBENCH command.
bool ml_bench_command(const char *line, bool sampling);

// Summary of one set of timings in microseconds.
typedef struct {
    uint32_t median, p95, min, max, mean;
} ml_bench_stat_t;

// Sorts v[0..n-1] in place and fills *out; n must be at least 1. The median
// of an even count is the mean of the two middle values; p95 is the
// nearest-rank 95th percentile; the mean is rounded to the nearest integer.
void ml_bench_stats(uint32_t *v, int n, ml_bench_stat_t *out);

// The time of the last Invoke() inside InferenceTask, in microseconds
// (inference_mtk.cpp); 0 before the first inference.
uint32_t inference_mtk_last_invoke_us(void);

#ifdef __cplusplus
}
#endif
