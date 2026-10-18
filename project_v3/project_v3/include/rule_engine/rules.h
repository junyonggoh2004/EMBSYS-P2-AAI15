#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rule_engine/rule_expr.h"   // brings in token_kind_t, fkind_t, rpn_t

#ifdef __cplusplus
extern "C" {
#endif

// ---- shared tunables / limits -----------------------------------------------
#ifndef MAX_RULES
#define MAX_RULES  8
#endif

#ifndef VAR_MAX
#define VAR_MAX   12   // headroom for multi-calc motion rules
#endif

#ifndef RPN_MAX
#define RPN_MAX    48
#endif

// ---- Rule structure (same layout as old rules.c) ----------------------------
typedef struct {
  char name[32];
  char source[32];

  // calc variables
  int   var_count;
  char  var_name[VAR_MAX][16];
  rpn_t var_rpn[VAR_MAX][RPN_MAX];
  int   var_rpn_len[VAR_MAX];

  // condition
  rpn_t when_rpn[RPN_MAX];
  int   when_len;

  // action (parsed minimally)
  char  action[256];

  // Previous snapshot of computed variables
  double last_vval[VAR_MAX];
  bool   prev_init;

  // current-sample values for logging
  double curr_vval[VAR_MAX];
  int    curr_vcnt;
  bool   curr_valid;
} rule_t;


// ---- Public API -------------------------------------------------------------

// Init / lifecycle
void rules_init(void);
void rules_reset(void);
void rules_dump(void);

// Evaluate all rules tied to `source` (sensor name) for this raw frame
void rules_on_sample(const char *source,
                     const uint8_t *buf, int len,
                     uint32_t ts_ms);

// BEGINRULE … ENDRULE streaming loader (works for REPL and MQTT)
void rules_begin(void);
void rules_feed_line(const char *line);  // e.g., "name=GY511_SHAKE"
bool rules_end(void);                    // returns true on success
bool rules_get_var(const rule_t *R, const char *name, double *out);

#ifdef __cplusplus
}
#endif
