// rule_engine/rules.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"

#include "rule_engine/rules.h"
#include "rule_engine/rule_expr.h"   // to_rpn, eval_ctx_t, eval_rpn, rpn_t
#include "rule_engine/rule_action.h" // fire_action()
#include "inference_engine/inference_manager.h" // ML models



// ===== Global rule store =====================================================
static rule_t g_rules[MAX_RULES];
static int    g_rules_n = 0;

// building a rule (BEGINRULE..ENDRULE)
static rule_t s_build;
static bool   s_building = false;

// ===== Utility ===============================================================
static inline void trim_spaces(char *s) {
    if (!s) return;
    // left
    char *p = s;
    while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    // right
    size_t n = strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n')) s[--n] = 0;
}

// ===== Public API: lifecycle =================================================
void rules_init(void) {
    g_rules_n   = 0;
    s_building  = false;
    memset(g_rules, 0, sizeof(g_rules));
}

void rules_reset(void) {
    g_rules_n   = 0;
    s_building  = false;
    memset(g_rules, 0, sizeof(g_rules));
    printf("[RULE] reset\n");
}

void rules_dump(void) {
    printf("[RULE] count=%d\n", g_rules_n);
    for (int i=0; i<g_rules_n; i++) {
        const rule_t *R = &g_rules[i];
        printf("  #%d name=%s src=%s vars=%d action=%s prev_init=%d\n",
               i, R->name, R->source, R->var_count,
               R->action, R->prev_init ? 1 : 0);
    }
}

// ===== BEGINRULE…ENDRULE builder ============================================
void rules_begin(void) {
    memset(&s_build, 0, sizeof(s_build));
    s_build.var_count = 0;
    s_build.prev_init = false;
    s_building        = true;
}

// calc=<lhs=expr>
static bool parse_calc_line(const char *v) {
    // form: calc=lhs=expr
    const char *p = strchr(v, '=');
    if (!p) return false;   // no '=' after "calc"
    p++;                    // move to lhs start

    char lhs[16] = {0};
    int  li      = 0;

    // collect LHS up to next '='
    while (*p && *p!='=' && li<15) {
        lhs[li++] = *p++;
    }
    lhs[li] = 0;
    if (*p != '=') return false;
    p++;    // expression start

    if (s_build.var_count >= VAR_MAX) return false;
    int idx = s_build.var_count++;

    strncpy(s_build.var_name[idx], lhs,
            sizeof(s_build.var_name[idx]) - 1);
    s_build.var_name[idx][sizeof(s_build.var_name[idx]) - 1] = 0;

    // parse expression to RPN (delegated to rule_expr)
    char expr[128];
    strncpy(expr, p, sizeof(expr) - 1);
    expr[sizeof(expr) - 1] = 0;
    trim_spaces(expr);

    int rn = to_rpn(expr, s_build.var_rpn[idx], RPN_MAX);
    if (rn < 0) return false;
    s_build.var_rpn_len[idx] = rn;
    return true;
}

// when=<expr>
static bool parse_when_line(const char *v) {
    char expr[128];
    strncpy(expr, v, sizeof(expr) - 1);
    expr[sizeof(expr) - 1] = 0;
    trim_spaces(expr);

    int rn = to_rpn(expr, s_build.when_rpn, RPN_MAX);
    if (rn < 0) return false;
    s_build.when_len = rn;
    return true;
}

// action=<spec> (log:/gpio: etc.)
static bool parse_action_line(const char *v) {
    strncpy(s_build.action, v, sizeof(s_build.action) - 1);
    s_build.action[sizeof(s_build.action) - 1] = 0;
    trim_spaces(s_build.action);
    return true;
}

// infer=v1,v2,v3
static bool parse_infer_line(const char *v) {
    char buf[256];
    strncpy(buf, v, sizeof(buf)-1);
    buf[sizeof(buf)-1] = 0;
    trim_spaces(buf);
    
    char *p = buf;
    while (*p && s_build.infer_cnt < 16) {
        char *comma = strchr(p, ',');
        if (comma) *comma = 0;
        trim_spaces(p);
        if (strlen(p) > 0) {
            strncpy(s_build.infer_vars[s_build.infer_cnt], p, 15);
            s_build.infer_vars[s_build.infer_cnt][15] = 0;
            s_build.infer_cnt++;
        }
        if (!comma) break;
        p = comma + 1;
    }
    return true;
}

void rules_feed_line(const char *line) {
    if (!s_building || !line) return;

    // Accept forms:
    // name=<...>
    // source=<...>
    // calc=<lhs=expr>
    // when=<expr>
    // action=<spec>

    if (!strncmp(line, "name=", 5)) {
        strncpy(s_build.name, line + 5, sizeof(s_build.name) - 1);
        s_build.name[sizeof(s_build.name) - 1] = 0;
        trim_spaces(s_build.name);
        return;
    }
    if (!strncmp(line, "source=", 7)) {
        strncpy(s_build.source, line + 7, sizeof(s_build.source) - 1);
        s_build.source[sizeof(s_build.source) - 1] = 0;
        trim_spaces(s_build.source);
        return;
    }
    if (!strncmp(line, "calc=", 5)) {
        (void)parse_calc_line(line);   // tolerant
        return;
    }
    if (!strncmp(line, "when=", 5)) {
        (void)parse_when_line(line + 5);
        return;
    }
    if (!strncmp(line, "infer=", 6)) {
        (void)parse_infer_line(line + 6);
        return;
    }
    if (!strncmp(line, "action=", 7)) {
        (void)parse_action_line(line + 7);
        return;
    }

    // unknown line -> ignore (extensible)
}

bool rules_end(void) {
    if (!s_building) return false;
    s_building = false;

    if (g_rules_n >= MAX_RULES) {
        printf("[RULE] table full\n");
        return false;
    }
    if (s_build.source[0] == 0) {
        printf("[RULE] missing source=\n");
        return false;
    }

    // when missing, default to TRUE (always fire on any frame)
    if (s_build.when_len == 0) {
        rpn_t t; t.k = TK_NUM; t.num = 1.0;
        s_build.when_rpn[0] = t;
        s_build.when_len    = 1;
    }

    // clear prev snapshot
    for (int k=0; k<VAR_MAX; k++) s_build.last_vval[k] = 0.0;
    s_build.prev_init = false;

    g_rules[g_rules_n++] = s_build;
    printf("[RULE] added name=%s src=%s vars=%d action=%s\n",
           s_build.name, s_build.source,
           s_build.var_count, s_build.action);
    return true;
}

// ===== Per-sample evaluation =================================================
void rules_on_sample(const char *source,
                     const uint8_t *buf, int len,
                     uint32_t ts_ms)
{
    if (!source || !buf || len <= 0) return;

    absolute_time_t t_start = get_absolute_time();
    int rules_fired = 0;

    for (int i=0; i<g_rules_n; i++) {
        rule_t *R = &g_rules[i];
        if (strcmp(source, R->source) != 0) continue;

        // compute variables in order; each var may reference earlier ones
        double vval[VAR_MAX] = {0};

        for (int j=0; j<R->var_count; j++) {
            eval_ctx_t E = {
                .buf       = buf,
                .len       = len,
                .vname     = (const char (*)[16])R->var_name,
                .vval      = vval,
                .vcnt      = j,              // only previous vars visible
                .prev_vval = R->last_vval,
                .have_prev = R->prev_init,
                .total_vcnt = R->var_count,
                .rule_ptr  = R
            };
            vval[j] = eval_rpn(&E, R->var_rpn[j], R->var_rpn_len[j]);

            // Push to history buffer for sliding windows
            window_buf_t *H = &R->history[j];
            H->val[H->head] = (float)vval[j];
            H->ts[H->head]  = ts_ms;
            H->head = (H->head + 1) % WINDOW_MAX_SAMPLES;
            if (H->count < WINDOW_MAX_SAMPLES) H->count++;
        }

        for (int j=0; j<R->var_count; j++) {
            R->curr_vval[j] = vval[j];
        }
        R->curr_vcnt  = R->var_count;
        R->curr_valid = true;
        
        // -----------------------------------------------------------
        // INFERENCE PIPELINE
        // -----------------------------------------------------------
        if (R->infer_cnt > 0 && inference_mgr_is_ready()) {
            float infer_inputs[INFER_MAX_INPUTS] = {0};
            int mapped_cnt = 0;
            
            // Map the resolved variables into the inference inputs array
            for (int k = 0; k < R->infer_cnt && k < INFER_MAX_INPUTS; k++) {
                double val = 0.0;
                // Find matching variable name in vval[]
                for (int m = 0; m < R->var_count; m++) {
                    if (strcmp(R->infer_vars[k], R->var_name[m]) == 0) {
                        val = vval[m];
                        break;
                    }
                }
                infer_inputs[mapped_cnt++] = (float)val;
            }
            
            inference_mgr_set_inputs(infer_inputs, mapped_cnt);
            inference_mgr_run();
        }

        // evaluate WHEN with all variables available (and prev visible)
        eval_ctx_t Ew = {
            .buf       = buf,
            .len       = len,
            .vname     = (const char (*)[16])R->var_name,
            .vval      = vval,
            .vcnt      = R->var_count,
            .prev_vval = R->last_vval,
            .have_prev = R->prev_init,
            .total_vcnt = R->var_count,
            .rule_ptr  = R
        };
        double ok = eval_rpn(&Ew, R->when_rpn, R->when_len);

        if (ok != 0.0) {
            // “value” to pass to action: latest var if any, else 1.0
            double value = (R->var_count > 0)
                           ? vval[R->var_count - 1]
                           : 1.0;
            fire_action(R, value, ts_ms);   // in rule_action.c
        }

        // AFTER evaluation: update prev snapshot for next frame
        for (int j=0; j<R->var_count; j++) R->last_vval[j] = vval[j];
        R->prev_init = true;   // now we have a valid previous frame
        rules_fired++;
    }

    int64_t dt = absolute_time_diff_us(t_start, get_absolute_time());
    if (rules_fired > 0 || dt > 500) {
        printf("[TRACE] Total Rule Eval (%s): %lldus\n", source, (long long)dt);
    }
}

bool rules_get_var(const rule_t *R, const char *name, double *out) {
    if (!R || !name || !out) return false;

    // Prefer current-sample values if available
    if (R->curr_valid) {
        for (int i = 0; i < R->curr_vcnt; i++) {
            if (strcmp(name, R->var_name[i]) == 0) {
                *out = R->curr_vval[i];
                return true;
            }
        }
    }

    // Fallback: last-sample values (for cases where action fires later)
    if (R->prev_init) {
        for (int i = 0; i < R->var_count; i++) {
            if (strcmp(name, R->var_name[i]) == 0) {
                *out = R->last_vval[i];
                return true;
            }
        }
    }

    return false;
}
