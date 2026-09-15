#pragma once

/* 
 * model_params.h — Baked-in normalization and threshold defaults
 * Extracted from norm_stats (2).json and thresholds (2).json
 */

#ifndef MODEL_PARAMS_H
#define MODEL_PARAMS_H

#include <stdint.h>

#define BASE_MODEL_FEATURES   16
#define BASE_MODEL_THRESHOLDS 16

static const float g_base_model_means[BASE_MODEL_FEATURES] = {
    28.637722f, 0.0f, 28.637722f, 28.637722f,
    79.230476f, 0.0f, 79.230476f, 79.230476f,
    0.597104f, 0.056457f, 0.529803f, 0.682793f,
    83.053177f, 18.581749f, 62.705151f, 99.319275f
};

static const float g_base_model_stds[BASE_MODEL_FEATURES] = {
    10.688627f, 1.0f, 10.688627f, 10.688627f,
    29.649685f, 1.0f, 29.649685f, 29.649685f,
    0.440117f, 0.185806f, 0.326389f, 0.652547f,
    66.500793f, 28.877604f, 71.994102f, 70.504059f
};

typedef struct {
    char  mask[5];
    float threshold;
} base_thresh_entry_t;

static const base_thresh_entry_t g_base_model_thresholds[BASE_MODEL_THRESHOLDS] = {
    {"0001", 7.296454f},
    {"0010", 1.592735f},
    {"0011", 4.307728f},
    {"0100", 1.303289f},
    {"0101", 4.079598f},
    {"0110", 1.170117f},
    {"0111", 3.127711f},
    {"1000", 1.140673f},
    {"1001", 4.494464f},
    {"1010", 1.279026f},
    {"1011", 3.186591f},
    {"1100", 1.318910f},
    {"1101", 3.382835f},
    {"1110", 1.190980f},
    {"1111", 2.668663f},
    {"0000", 999999.0f} // fallback
};

#endif
