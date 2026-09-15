#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "bus/bus_common.h"
#include "application/scheduler.h"
#include "mqtt/mqtt_telemetry.h"
#include "mqtt/mqtt_json_parser.h"
#include "rule_engine/rules.h"

// Bind to flags from main loop
static bool *g_running = NULL;
static bool *g_cfg_ready = NULL;

void control_bind_flags(bool *running, bool *cfg_ready) {
    g_running = running;
    g_cfg_ready = cfg_ready;
}

/* -------- small helpers -------- */

static void trim_spaces(char *s) {
    if (!s) return;
    char *p = s;
    while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    size_t n = strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n] = 0;
}

/* -------- config block handler (existing) -------- */

// Split inline config payload and feed cfg_parse_*
static void cfg_apply_inline_block(const char *inline_block) {
    const char *begin = strstr(inline_block, "BEGINCFG");
    const char *end   = strstr(inline_block, "ENDCFG");
    if (!begin || !end || begin >= end) { printf("[CFG] invalid block\n"); return; }

    char delim = '|'; 
    if (!strchr(inline_block, delim) && strchr(inline_block, ';')) delim = ';';

    app_cfg_t tmp; memset(&tmp, 0, sizeof(tmp));
    // Provide sane defaults just like REPL
    extern void repl_init_defaults(app_cfg_t *cfg);
    extern void cfg_parse_begin(void);
    extern void cfg_parse_line(const char *line, app_cfg_t *cfg);
    extern void cfg_parse_end(app_cfg_t *cfg);

    cfg_parse_begin();
    const char *p = begin + (int)strlen("BEGINCFG");
    if (*p == delim) p++;
    const char *cur = p;
    char token[256];

    while (cur < end) {
        const char *next = memchr(cur, delim, (size_t)(end - cur));
        size_t n = (size_t)((next ? next : end) - cur);
        if (n >= sizeof(token)) n = sizeof(token) - 1;
        memcpy(token, cur, n); token[n] = 0;

        // trim spaces
        char *b = token; 
        while (*b==' '||*b=='\t'||*b=='\r'||*b=='\n') b++;
        char *e = b + strlen(b); 
        while (e>b && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) *--e=0;

        if (*b && strcmp(b,"BEGINCFG") && strcmp(b,"ENDCFG")) {
            if (!tmp.name[0]) repl_init_defaults(&tmp); // seed once
            cfg_parse_line(b, &tmp);
        }
        if (!next) break;
        cur = next + 1;
    }
    cfg_parse_end(&tmp);

    int idx = scheduler_add_config(&tmp);
    if (idx >= 0) { 
        printf("[CFG] added #%d: %s\n", idx, tmp.name); 
        mqtt_pub_cfg_status(tmp.name, true);
        if (g_cfg_ready) *g_cfg_ready = true; 
    } else { 
        printf("[CFG] add failed (%d)\n", idx); 
        mqtt_pub_cfg_status(tmp.name, false);
    }
}


/* -------- rule block handler (NEW) -------- */

static void rules_apply_inline_block(const char *inline_block) {
    const char *begin = strstr(inline_block, "BEGINRULE");
    const char *end   = strstr(inline_block, "ENDRULE");
    if (!begin || !end || begin >= end) {
        printf("[RULE] invalid block\n");
        return;
    }

    char delim = '|';
    if (!strchr(inline_block, delim) && strchr(inline_block, ';')) delim = ';';

    rules_begin();

    const char *p = begin + (int)strlen("BEGINRULE");
    if (*p == delim) p++;
    const char *cur = p;
    char token[256];

    char rule_name[32];
    rule_name[0] = 0;  // we'll fill this if we see "name=..."

    while (cur < end) {
        const char *next = memchr(cur, delim, (size_t)(end - cur));
        size_t n = (size_t)((next ? next : end) - cur);
        if (n >= sizeof(token)) n = sizeof(token) - 1;
        memcpy(token, cur, n);
        token[n] = 0;

        trim_spaces(token);
        if (*token && strcmp(token,"BEGINRULE") && strcmp(token,"ENDRULE")) {
            // Capture name=... for status publishing
            if (!strncasecmp(token, "name=", 5)) {
                const char *v = token + 5;
                while (*v == ' ' || *v == '\t') v++;
                size_t L = strlen(v);
                if (L >= sizeof(rule_name)) L = sizeof(rule_name) - 1;
                memcpy(rule_name, v, L);
                rule_name[L] = 0;
            }

            // Still feed everything into the rule engine
            rules_feed_line(token);
        }

        if (!next) break;
        cur = next + 1;
    }

    bool ok = rules_end();

    const char *reported_name = (rule_name[0] ? rule_name : "UNKNOWN");

    if (ok) {
        printf("[RULE] added name=%s\n", reported_name);
        mqtt_pub_rule_status(reported_name, true);
    } else {
        printf("[RULE] add failed for name=%s\n", reported_name);
        mqtt_pub_rule_status(reported_name, false);
    }
}


/* -------- multi-block dispatcher (CFG + RULE) -------- */

static void apply_legacy_blocks(const char *multi) {
    const char *p = multi;
    while (p && *p) {
        const char *b_cfg  = strstr(p, "BEGINCFG");
        const char *b_rule = strstr(p, "BEGINRULE");

        const char *begin = NULL;
        enum { BK_NONE, BK_CFG, BK_RULE } kind = BK_NONE;

        if (b_cfg && (!b_rule || b_cfg < b_rule)) {
            begin = b_cfg; kind = BK_CFG;
        } else if (b_rule) {
            begin = b_rule; kind = BK_RULE;
        } else {
            break;
        }

        const char *end_marker = (kind == BK_CFG) ? "ENDCFG" : "ENDRULE";
        const char *end = strstr(begin, end_marker);
        if (!end) break;
        end += strlen(end_marker);

        size_t L = (size_t)(end - begin);
        char block[512];
        if (L >= sizeof(block)) L = sizeof(block) - 1;
        memcpy(block, begin, L);
        block[L] = 0;

        if (kind == BK_CFG)  cfg_apply_inline_block(block);
        else if (kind == BK_RULE) rules_apply_inline_block(block);

        p = end;
    }
}

/* -------- command handler (unchanged) -------- */

extern void scheduler_reset(void);
extern void rules_reset(void);
extern void sensor_bridge_reset(void);

static void cmd_cb(const char *cmd) {
    if (!cmd || !*cmd || !g_running) return;

    if (!strcasecmp(cmd, "RUN")) {
        *g_running = true;
        printf("[CMD] RUN\n");  
    }
    else if (!strcasecmp(cmd, "STOP")) {
        *g_running = false;
        printf("[CMD] STOP\n");
    }
    else if (!strcasecmp(cmd, "CLEAR")) {
        // Stop scheduler first so nothing runs mid-reset
        *g_running = false;

        // Reset everything: scheduler, rules, bridge mappings
        scheduler_reset();
        rules_reset();
        sensor_bridge_reset();

        // No configs loaded after CLEAR → cfg_ready = false
        if (g_cfg_ready) *g_cfg_ready = false;

        printf("[CMD] CLEAR (scheduler + rules + bridge)\n");
    }
    else if (!strcasecmp(cmd, "SHOW")) {
        extern void scheduler_dump(void);
        printf("[CMD] SHOW\n");
        scheduler_dump();
        rules_dump();
    }
    else {
        printf("[CMD] Unknown: %s\n", cmd);
    }
}


/* -------- config topic callback (JSON + legacy) -------- */

static void cfg_cb(const char *payload) {
    if (!payload) return;

    printf("[CFG] cfg_cb payload (first 120): %.*s\n", 120, payload);

    char buf[2048];
    int conv = mqtt_json_try_convert_to_inline(payload, buf, sizeof(buf));

    printf("[CFG] mqtt_json_try_convert_to_inline -> %d\n", conv);
    if (conv > 0) {
        buf[conv] = 0; // ensure nul-terminated for printing
        printf("[CFG] inline from JSON: %s\n", buf);
        // JSON successfully converted → may contain multiple CFG/RULE blocks
        apply_legacy_blocks(buf);
        return;
    }

    // Not JSON, or JSON failed → treat as legacy inline
    if (strstr(payload, "BEGINCFG") || strstr(payload, "BEGINRULE")) {
        //printf("[CFG] treating payload as legacy inline\n");
        apply_legacy_blocks(payload);
    } else {
        // plain text on config topic → ignore or log
        printf("[CFG] unrecognized payload (no BEGINCFG/BEGINRULE)\n");
    }
}


/* -------- node topic callback (legacy CFG or CMD) -------- */

static void node_cb(const char *from, const char *payload) {
    (void)from;
    if (!payload) return;

    if (strstr(payload,"BEGINCFG") || strstr(payload,"BEGINRULE")) {
        apply_legacy_blocks(payload);
    } else {
        cmd_cb(payload);
    }
}

// Call this after Wi-Fi is connected
bool control_mqtt_start(const char *broker, uint16_t port, const char *client_id, const char *node_id) {
    mqtt_set_node_id(node_id);
    return mqtt_init(broker, port, client_id, cfg_cb, cmd_cb, node_cb);
}
