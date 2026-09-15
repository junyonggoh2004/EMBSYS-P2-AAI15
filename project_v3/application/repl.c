#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#include "bus/bus_common.h"
#include "application/output_format.h"
#include "application/scheduler.h"
#include "rule_engine/rules.h"

// --- Small helpers -----------------------------------------------------------
static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
}
static void lower_str(char *s) {
    for (; *s; ++s) *s = (char)tolower(*s);
}
static void trim_ws_inplace(char *s) {
    if (!s) return;
    // left
    char *p = s;
    while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    // right
    size_t n = strlen(s);
    while (n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n] = 0;
}

// --- REPL state machine ------------------------------------------------------
typedef enum {
    RS_IDLE = 0,
    RS_CFG,
    RS_RULE
} repl_state_t;

static repl_state_t s_state = RS_IDLE;
static app_cfg_t s_tmp_cfg;

// --- inline split helpers ----------------------------------------------------
static void split_feed_by_delim(const char *between, char delim,
                                void (*feed)(const char *one)) {
    const char *p = between;
    const char *start = between;
    char buf[256];

    while (*p) {
        if (*p == '\\') {                 // allow escaping: \|
            if (p[1]) { p += 2; continue; }
        }
        if (*p == delim) {
            size_t n = (size_t)(p - start);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, start, n);
            buf[n] = 0;
            trim_ws_inplace(buf);
            if (*buf) feed(buf);
            p++; start = p;
        } else {
            p++;
        }
    }
    if (start && *start) {
        size_t n = (size_t)(p - start);
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, start, n);
        buf[n] = 0;
        trim_ws_inplace(buf);
        if (*buf) feed(buf);
    }
}

// --- inline config feeder ----------------------------------------------------
static void feed_inline_cfg_token(const char *line) {
    if (!line) return;
    if (!strcmp(line, "BEGINCFG") || !strcmp(line, "ENDCFG")) return;
    cfg_parse_line(line, &s_tmp_cfg);
}
static void feed_inline_cfg_block(const char *s_between, char delim, app_cfg_t *cfg) {
    cfg_parse_begin();
    split_feed_by_delim(s_between, delim, feed_inline_cfg_token);
    cfg_parse_end(cfg);
    printf("OK: Config applied (inline, delim='%c')\n", delim);
}

// --- inline rule feeder ------------------------------------------------------
static void feed_inline_rule_token(const char *line) {
    if (!line) return;
    if (!strcmp(line, "BEGINRULE") || !strcmp(line, "ENDRULE")) return;
    rules_feed_line(line);
}
static bool feed_inline_rule_block(const char *s_between, char delim) {
    rules_begin();
    split_feed_by_delim(s_between, delim, feed_inline_rule_token);
    return rules_end();
}

// --- REPL helpers ------------------------------------------------------------
void repl_init_defaults(app_cfg_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->proto   = proto_i2c;
    cfg->mode    = sample_mode_poll;
    cfg->freq_hz = 1;
    strncpy(cfg->name, "SENSOR", sizeof(cfg->name)-1);
    cfg->i2c_sda=4; cfg->i2c_scl=5; cfg->i2c_baud=100000;
    cfg->i2c_addr=0x76; cfg->i2c_reg_size=1; cfg->i2c_read_len=1; cfg->i2c_restart=true;
    cfg->uart_tx=0; cfg->uart_rx=1; cfg->uart_baud=9600;
    cfg->uart_bits=8; cfg->uart_parity='N'; cfg->uart_stop=1; cfg->uart_read_len=64;
    cfg->gpio_pin=2; cfg->gpio_trig=14; cfg->gpio_echo=15; cfg->gpio_pulse_timeout_us=30000;
}

void repl_show_help(void) {
    printf("Commands:\n");
    printf("  BEGINCFG ... ENDCFG        - add sensor config (multi-line)\n");
    printf("  BEGINCFG|k=v|...|ENDCFG    - add sensor config (inline with '|', supports \\|)\n");
    printf("  BEGINRULE ... ENDRULE      - add rule (multi-line)\n");
    printf("  BEGINRULE|k=v|...|ENDRULE  - add rule (inline with '|', supports \\|)\n");
    printf("  RUN / STOP                 - control sampling\n");
    printf("  CLEAR                      - clear sensors + rules\n");
    printf("  SHOW / SHOWRULES           - show sensors / rules\n");
    printf("  CLEARRULES                 - clear only rules\n");
    printf("  HELP                       - this text\n");
    printf("\n");
    printf("Example inline rule (HMC5883L big-endian X,Z,Y at 0x03):\n");
    printf("BEGINRULE|name=GY511_MAG|source=GY511_ACC|calc=mx=s16be(0)|calc=mz=s16be(2)|calc=my=s16be(4)|calc=mag=sqrt(mx*mx+my*my+mz*mz)|when=mag>350|action=log:MAG change detected|ENDRULE\n");
}

// --- main REPL loop ----------------------------------------------------------
void repl_poll(app_cfg_t *unused, bool *running, bool *cfg_ready) {
    (void)unused;
    if (!running || !cfg_ready) return;

    static char line[512];
    static size_t line_len = 0;
    int ch;

    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r') continue;

        if (ch == '\n') {
            line[line_len] = 0;
            trim_crlf(line);

            // ---------------- STATE: IDLE ----------------
            if (s_state == RS_IDLE) {
                // Inline CONFIG
                char *b_cfg = strstr(line, "BEGINCFG");
                char *e_cfg = strstr(line, "ENDCFG");
                if (b_cfg && e_cfg && b_cfg < e_cfg) {
                    char delim = strchr(line, '|') ? '|' : (strchr(line,';')?';':'|');
                    memset(&s_tmp_cfg, 0, sizeof(s_tmp_cfg));
                    repl_init_defaults(&s_tmp_cfg);

                    char *payload_start = b_cfg + (int)strlen("BEGINCFG");
                    if (*payload_start == delim) payload_start++;

                    ptrdiff_t raw_len = (ptrdiff_t)(e_cfg - payload_start);
                    if (raw_len < 0) {
                        printf("ERR: malformed inline config\n");
                    } else {
                        size_t len = (size_t)raw_len;
                        char *between = (char*)malloc(len+1);
                        if (!between) {
                            printf("ERR: alloc fail\n");
                        } else {
                            memcpy(between, payload_start, len);
                            between[len] = 0;
                            feed_inline_cfg_block(between, delim, &s_tmp_cfg);
                            free(between);

                            int idx = scheduler_add_config(&s_tmp_cfg);
                            if (idx >= 0)
                                printf("CFG: ok (added #%d: %s)\n", idx, s_tmp_cfg.name);
                            else
                                printf("CFG: add failed (%d)\n", idx);
                            *cfg_ready = true;
                        }
                    }
                    line_len = 0;
                    continue;
                }

                // Inline RULE
                char *b_rule = strstr(line, "BEGINRULE");
                char *e_rule = strstr(line, "ENDRULE");
                if (b_rule && e_rule && b_rule < e_rule) {
                    char delim = strchr(line, '|') ? '|' : (strchr(line,';')?';':'|');

                    char *payload_start = b_rule + (int)strlen("BEGINRULE");
                    if (*payload_start == delim) payload_start++;

                    ptrdiff_t raw_len = (ptrdiff_t)(e_rule - payload_start);
                    if (raw_len < 0) {
                        printf("ERR: malformed inline rule\n");
                    } else {
                        size_t len = (size_t)raw_len;
                        char *between = (char*)malloc(len+1);
                        if (!between) {
                            printf("ERR: alloc fail\n");
                        } else {
                            memcpy(between, payload_start, len);
                            between[len] = 0;
                            bool ok = feed_inline_rule_block(between, delim);
                            free(between);
                            printf(ok ? "RULE: ok\n" : "RULE: parse error\n");
                        }
                    }
                    line_len = 0;
                    continue;
                }

                // ---------------- COMMANDS ----------------
                char cmd[32]; strncpy(cmd, line, sizeof(cmd)-1); cmd[sizeof(cmd)-1]=0; lower_str(cmd);

                if (strcmp(cmd, "begincfg") == 0) {
                    repl_init_defaults(&s_tmp_cfg);
                    cfg_parse_begin();
                    s_state = RS_CFG;
                    printf("CFG: begin\n");
                }
                else if (strcmp(cmd, "beginrule") == 0) {
                    rules_begin();
                    s_state = RS_RULE;
                    printf("RULE: begin\n");
                }
                else if (strcmp(cmd, "run") == 0) { *running = true; printf("RUN\n"); }
                else if (strcmp(cmd, "stop") == 0) { *running = false; printf("STOP\n"); }
                else if (strcmp(cmd, "clear") == 0) {
                    scheduler_reset();
                    rules_reset();
                    printf("CLEARED\n");
                }
                else if (strcmp(cmd, "clearrules") == 0) {
                    rules_reset();
                }
                else if (strcmp(cmd, "show") == 0) {
                    printf("[SHOW] sampling=%u\n", *running?1u:0u);
                    extern void scheduler_dump(void);
                    scheduler_dump();
                    rules_dump();
                }
                else if (strcmp(cmd, "showrules") == 0) {
                    rules_dump();
                }
                else if (strcmp(cmd, "help") == 0) {
                    repl_show_help();
                }
                else if (line_len) {
                    printf("Unknown: %s\n", line);
                }
            }

            // ---------------- STATE: CFG ----------------
            else if (s_state == RS_CFG) {
                if (strcasecmp(line, "ENDCFG") == 0) {
                    cfg_parse_end(&s_tmp_cfg);
                    s_state = RS_IDLE;
                    int idx = scheduler_add_config(&s_tmp_cfg);
                    if (idx >= 0)
                        printf("CFG: ok (added #%d: %s)\n", idx, s_tmp_cfg.name);
                    else
                        printf("CFG: add failed\n");
                    *cfg_ready = true;
                } else {
                    cfg_parse_line(line, &s_tmp_cfg);
                }
            }

            // ---------------- STATE: RULE ----------------
            else if (s_state == RS_RULE) {
                if (strcasecmp(line, "ENDRULE") == 0) {
                    if (rules_end()) printf("RULE: ok\n");
                    else             printf("RULE: parse error\n");
                    s_state = RS_IDLE;
                } else {
                    rules_feed_line(line);
                }
            }

            line_len = 0;
        } else {
            if (line_len + 1 < sizeof(line)) {
                line[line_len++] = (char)ch;
            } else {
                line[line_len] = 0;
                printf("Line too long\n");
                line_len = 0;
            }
        }
    }
}
