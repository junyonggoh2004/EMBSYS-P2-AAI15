#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Token / function enums (from original rules.c) -------------------------
typedef enum {
  TK_NUM = 0,
  TK_ID,
  TK_FUNC,
  TK_PLUS, TK_MINUS, TK_MUL, TK_DIV, TK_MOD,
  TK_NOT,
  TK_LT, TK_LE, TK_GT, TK_GE, TK_EQ, TK_NE,
  TK_AND, TK_OR,
  TK_LP, TK_RP, TK_COMMA,
  TK_END
} token_kind_t;

typedef enum {
  FN_u8 = 0, FN_s8,
  FN_u16le, FN_s16le,
  FN_u16be, FN_s16be,
  FN_abs, FN_sqrt,
  FN_mag3
} fkind_t;

// ---- RPN node ---------------------------------------------------------------
typedef struct {
  token_kind_t k;
  union {
    double num;   // TK_NUM
    int    op;    // operators / function code
    char   id[16]; // TK_ID identifier name
  };
} rpn_t;

// ---- Evaluation context (old eval_ctx_t) ------------------------------------
typedef struct {
  const uint8_t *buf; int len;
  const char (*vname)[16];  // array of variable names
  const double *vval;       // array of variable values
  int vcnt;                 // how many vars are visible

  // Previous variable values
  const double *prev_vval;  // parallel to vname; may be NULL
  bool have_prev;
} eval_ctx_t;

// ---- Public expr API used by rules.c ----------------------------------------

// Parse expression string into RPN; returns length or <0 on error
int to_rpn(const char *expr, rpn_t *out, int out_cap);

// Evaluate RPN with given eval context
double eval_rpn(eval_ctx_t *e, const rpn_t *code, int n);

#ifdef __cplusplus
}
#endif
