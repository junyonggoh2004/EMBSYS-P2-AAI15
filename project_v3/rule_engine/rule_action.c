// rule_engine/rule_action.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"       // NEW: for PWM control

#include "mqtt/mqtt_telemetry.h"
#include "rule_engine/rule_action.h"
#include "inference_engine/inference_manager.h"

extern bool rules_get_var(const rule_t *R, const char *name, double *out);

// Helper: drive a GPIO pin to 0/1
static void gpio_drive(int pin, int level) {
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, GPIO_OUT);
    gpio_put((uint)pin, level ? 1 : 0);
}

// Helper: set a PWM duty cycle on a given GPIO pin
// duty_frac in [0.0 .. 1.0]
static void pwm_set_duty_frac(int pin, float duty_frac) {
    if (duty_frac < 0.0f) duty_frac = 0.0f;
    if (duty_frac > 1.0f) duty_frac = 1.0f;

    gpio_set_function((uint)pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num((uint)pin);
    uint chan  = pwm_gpio_to_channel((uint)pin);

    // Simple default config: 10 kHz-ish on default clk
    pwm_config cfg = pwm_get_default_config();
    // You can tweak divider/wrap later if you want different freq/resolution
    pwm_config_set_clkdiv(&cfg, 4.0f);      // reasonably high frequency
    pwm_config_set_wrap(&cfg, 1000);        // 0..1000 => ~10-bit resolution
    pwm_init(slice, &cfg, false);

    uint16_t level = (uint16_t)(duty_frac * 1000.0f);
    pwm_set_chan_level(slice, chan, level);
    pwm_set_enabled(slice, true);
}

/* Helper: publish config/rule to a specific node
   action string will contain the inline BEGINCFG/BEGINRULE block. */
        static void send_cfg_to_node(const char *node_id, const char *cfg_block) {
            if (!node_id || !*node_id || !cfg_block || !*cfg_block) return;

            char topic[512];
            snprintf(topic, sizeof topic, "pico/%s/config", node_id);
            mqtt_pub_text(topic, "%s", cfg_block);
            printf("%s\n", cfg_block);
        }

/* Helper: publish cmd to a specific node */
static void send_cmd_to_node(const char *node_id, const char *cmd_payload) {
    if (!node_id || !*node_id || !cmd_payload || !*cmd_payload) return;

    char topic[128];
    snprintf(topic, sizeof topic, "pico/%s/cmd", node_id);
    mqtt_pub_text(topic, "%s", cmd_payload);
}

// Replace $value and $<varname> in the message.
// - $value  -> numeric "value" passed into fire_action()
// - $foo    -> rule variable "foo" (via rules_get_var)
// Unknown vars are left as literal "$foo".
static void substitute_vars(const rule_t *R,
                            double value,
                            const char *in,
                            char *out,
                            size_t outsz)
{
    if (!in || !out || outsz == 0) return;

    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < outsz; ) {
        if (in[i] != '$') {
            out[o++] = in[i++];
            continue;
        }

        // We saw '$'
        i++;
        char name[16];
        size_t n = 0;

        // collect [A-Za-z0-9_]+ as identifier
        while (in[i] &&
               ( (in[i] == '_') ||
                 (in[i] >= 'A' && in[i] <= 'Z') ||
                 (in[i] >= 'a' && in[i] <= 'z') ||
                 (in[i] >= '0' && in[i] <= '9') ) &&
               n < sizeof(name) - 1) {
            name[n++] = in[i++];
        }
        name[n] = 0;

        if (n == 0) {
            // Just a bare '$' -> emit it literally
            out[o++] = '$';
            continue;
        }

        double v;
        bool ok = false;

        if (strcmp(name, "value") == 0) {
            v  = value;
            ok = true;
        } else {
            ok = rules_get_var(R, name, &v);
        }

        if (!ok) {
            // Unknown var → output literally "$name"
            size_t need = 1 + strlen(name);
            if (o + need >= outsz) break;
            out[o++] = '$';
            memcpy(out + o, name, strlen(name));
            o += strlen(name);
            continue;
        }

        int written = snprintf(out + o, outsz - o, "%.3f", v);
        if (written < 0) written = 0;
        if ((size_t)written > outsz - o - 1) written = (int)(outsz - o - 1);
        o += (size_t)written;
    }

    out[o] = 0;
}



// Public dispatcher called from rules.c
void fire_action(const rule_t *R, double value, uint32_t ts_ms) {
    if (!R) return;
    const char *a = R->action;
    if (!a || !*a) return;

    absolute_time_t t_start = get_absolute_time();

    /* ---------------- batch: ... | ... ----------------
       Splits string by '|', trims spaces, and fires each action sequentially.
       Example: batch: infer.set:0:1.0 | infer.set:1:2.0 | infer.run
    -------------------------------------------------- */
    if (!strncmp(a, "batch:", 6)) {
        char buf[1024];
        strncpy(buf, a + 6, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *saveptr;
        char *token = strtok_r(buf, "|", &saveptr);
        while (token) {
            /* Trim leading spaces */
            while (*token == ' ' || *token == '\t') token++;
            
            /* Build temporary rule to fire action recursively */
            if (*token) {
                rule_t sub_rule = *R;
                strncpy(sub_rule.action, token, sizeof(sub_rule.action) - 1);
                sub_rule.action[sizeof(sub_rule.action) - 1] = '\0';
                
                /* Trim trailing spaces */
                size_t len = strlen(sub_rule.action);
                while (len > 0 && (sub_rule.action[len-1] == ' ' || sub_rule.action[len-1] == '\t')) {
                    sub_rule.action[--len] = '\0';
                }
                
                fire_action(&sub_rule, value, ts_ms);
            }
            token = strtok_r(NULL, "|", &saveptr);
        }
        int64_t dt = absolute_time_diff_us(t_start, get_absolute_time());
        printf("[TRACE] Rule Parse + Batch Actions executed in %lldus\n", (long long)dt);
        return;
    }

    /* ---------------- log:<text> ---------------- */
if (!strncmp(a, "log:", 4)) {
    const char *msg = a + 4;

    char rendered[160];
    substitute_vars(R, value, msg, rendered, sizeof(rendered));

    printf("[RULE] %s: %s (value=%.3f ts=%u)\n",
           R->name, rendered, value, (unsigned)ts_ms);

    mqtt_pub_event(R->name, "log", value, rendered, ts_ms);
    return;
}



    /* ---------------- gpio:<pin>=... ----------------
       Supported forms:
         gpio:<pin>=HIGH
         gpio:<pin>=LOW
         gpio:<pin>=TOGGLE
         gpio:<pin>=PULSE:<ms>
    -------------------------------------------------- */
    if (!strncmp(a, "gpio:", 5)) {
        const char *p = a + 5;
        int pin = 0;

        if (sscanf(p, "%d", &pin) != 1) {
            printf("[RULE] %s: bad gpio pin in action='%s'\n", R->name, a);
            return;
        }

        const char *eq = strchr(p, '=');
        if (!eq) {
            printf("[RULE] %s: gpio action missing '=' in '%s'\n", R->name, a);
            return;
        }
        eq++; // move past '='

        /* gpio:<pin>=HIGH */
        if (!strncmp(eq, "HIGH", 4)) {
            gpio_drive(pin, 1);
            printf("[RULE] %s: gpio %d=HIGH\n", R->name, pin);
            mqtt_pub_event(R->name, "gpio", value, "HIGH", ts_ms);
            return;
        }

        /* gpio:<pin>=LOW */
        if (!strncmp(eq, "LOW", 3)) {
            gpio_drive(pin, 0);
            printf("[RULE] %s: gpio %d=LOW\n", R->name, pin);
            mqtt_pub_event(R->name, "gpio", value, "LOW", ts_ms);
            return;
        }

        /* gpio:<pin>=TOGGLE */
        if (!strncmp(eq, "TOGGLE", 6)) {
            gpio_init((uint)pin);
            gpio_set_dir((uint)pin, GPIO_OUT);
            int cur = gpio_get((uint)pin);
            int next = !cur;
            gpio_put((uint)pin, next);

            printf("[RULE] %s: gpio %d=TOGGLE -> %d\n", R->name, pin, next);
            mqtt_pub_event(R->name, "gpio", value,
                           next ? "TOGGLE->HIGH" : "TOGGLE->LOW",
                           ts_ms);
            return;
        }

        /* gpio:<pin>=PULSE:<ms>  (e.g. PULSE:200) */
        if (!strncmp(eq, "PULSE:", 6)) {
            const char *ms_str = eq + 6;
            int ms = atoi(ms_str);
            if (ms <= 0) ms = 100; // default 100ms if bad/missing

            gpio_drive(pin, 1);
            sleep_ms((uint)ms);
            gpio_drive(pin, 0);

            printf("[RULE] %s: gpio %d=PULSE %dms\n", R->name, pin, ms);
            mqtt_pub_event(R->name, "gpio", value, "PULSE", ts_ms);
            return;
        }

        // Unknown gpio mode
        printf("[RULE] %s: unknown gpio action '%s'\n", R->name, eq);
        mqtt_pub_event(R->name, "gpio", value, "UNKNOWN", ts_ms);
        return;
    }

    /* ---------------- pwm:<pin>=... ----------------
       Supported forms:
         pwm:<pin>=<duty>
             - <duty> in 0..1   (fraction), e.g. pwm:15=0.5
             - or 0..100[%]     (percent), e.g. pwm:15=75 or pwm:15=75%
         pwm:<pin>=OFF
    -------------------------------------------------- */
    if (!strncmp(a, "pwm:", 4)) {
        const char *p = a + 4;
        int pin = 0;

        if (sscanf(p, "%d", &pin) != 1) {
            printf("[RULE] %s: bad pwm pin in action='%s'\n", R->name, a);
            return;
        }

        const char *eq = strchr(p, '=');
        if (!eq) {
            printf("[RULE] %s: pwm action missing '=' in '%s'\n", R->name, a);
            return;
        }
        eq++; // move past '='

        // OFF => disable PWM on that pin
        if (!strncmp(eq, "OFF", 3)) {
            gpio_set_function((uint)pin, GPIO_FUNC_SIO);
            gpio_drive(pin, 0);
            printf("[RULE] %s: pwm %d=OFF\n", R->name, pin);
            mqtt_pub_event(R->name, "pwm", value, "OFF", ts_ms);
            return;
        }

        // Parse duty: accept "0.5", "75", or "75%"
        char buf[32];
        strncpy(buf, eq, sizeof(buf)-1);
        buf[sizeof(buf)-1] = 0;

        // Strip trailing whitespace
        size_t L = strlen(buf);
        while (L && (buf[L-1] == '\r' || buf[L-1] == '\n' || buf[L-1] == ' ' || buf[L-1] == '\t')) {
            buf[--L] = 0;
        }

        int has_percent = 0;
        if (L > 0 && buf[L-1] == '%') {
            has_percent = 1;
            buf[L-1] = 0;
        }

        float duty_frac = 0.0f;
        float v = (float)atof(buf);

        if (has_percent) {
            duty_frac = v / 100.0f;
        } else {
            // If value > 1, treat as percent
            duty_frac = (v > 1.0f) ? (v / 100.0f) : v;
        }

        pwm_set_duty_frac(pin, duty_frac);

        printf("[RULE] %s: pwm %d duty=%.3f\n", R->name, pin, duty_frac);
        mqtt_pub_event(R->name, "pwm", value, buf, ts_ms);
        return;
    }

    /* ---------------- cfg.to:<node>:<BEGIN...> ----------------
       Example:
         cfg.to:pico-002:BEGINCFG|name=LED2|proto=gpio|...|ENDCFG
         cfg.to:remote-node-1:BEGINRULE|name=R|source=...|...|ENDRULE
       Payload can be a config or a rule; remote node treats it the same
       as if it came from mosquitto on pico/<node>/config.
    -------------------------------------------------- */
    if (!strncmp(a, "cfg.to:", 7)) {
        const char *rest = a + 7;
        const char *colon = strchr(rest, ':');
        if (!colon) {
            printf("[RULE] %s: cfg.to action missing second ':' in '%s'\n", R->name, a);
            mqtt_pub_event(R->name, "cfg.to", value, "PARSE_ERROR", ts_ms);
            return;
        }

        char node[64];
        size_t node_len = (size_t)(colon - rest);
        if (node_len >= sizeof(node)) node_len = sizeof(node) - 1;
        memcpy(node, rest, node_len);
        node[node_len] = 0;

        const char *payload = colon + 1;
        if (!*payload) {
            printf("[RULE] %s: cfg.to payload empty in '%s'\n", R->name, a);
            mqtt_pub_event(R->name, "cfg.to", value, "EMPTY_PAYLOAD", ts_ms);
            return;
        }
        printf("%s\n", payload);  
        send_cfg_to_node(node, payload);
        printf("[RULE] %s: cfg.to -> node=%s\n", R->name, node);
        mqtt_pub_event(R->name, "cfg.to", value, node, ts_ms);
        return;
    }

    /* ---------------- cmd.to:<node>:<COMMAND> ----------------
       Example:
         cmd.to:pico-001:RESET
         cmd.to:remote-node-1:START_STREAM
       This publishes to pico/<node>/cmd.
    -------------------------------------------------- */
    if (!strncmp(a, "cmd.to:", 7)) {
        const char *rest = a + 7;
        const char *colon = strchr(rest, ':');
        if (!colon) {
            printf("[RULE] %s: cmd.to action missing second ':' in '%s'\n", R->name, a);
            mqtt_pub_event(R->name, "cmd.to", value, "PARSE_ERROR", ts_ms);
            return;
        }

        char node[64];
        size_t node_len = (size_t)(colon - rest);
        if (node_len >= sizeof(node)) node_len = sizeof(node) - 1;
        memcpy(node, rest, node_len);
        node[node_len] = 0;

        const char *cmd = colon + 1;
        if (!*cmd) {
            printf("[RULE] %s: cmd.to payload empty in '%s'\n", R->name, a);
            mqtt_pub_event(R->name, "cmd.to", value, "EMPTY_PAYLOAD", ts_ms);
            return;
        }

        send_cmd_to_node(node, cmd);
        printf("[RULE] %s: cmd.to -> node=%s cmd='%s'\n", R->name, node, cmd);
        mqtt_pub_event(R->name, "cmd.to", value, node, ts_ms);
        return;
    }

    /* ---------- infer.set:<index>:<value> ----------
       Sets a single input feature on the inference engine.
       The Inference Manager will auto-normalize using stored params.
       Example: infer.set:4:$accX
    -------------------------------------------------- */
    if (!strncmp(a, "infer.set:", 10)) {
        const char *p = a + 10;
        int idx = 0;
        /* Parse index */
        idx = atoi(p);
        const char *colon2 = strchr(p, ':');
        if (!colon2) {
            printf("[RULE] %s: infer.set missing value in '%s'\n", R->name, a);
            return;
        }
        colon2++;

        /* Resolve the value (could be $varname) */
        char val_buf[32];
        substitute_vars(R, value, colon2, val_buf, sizeof(val_buf));
        float fval = (float)atof(val_buf);

        inference_mgr_set_input_slot(idx, fval);
        return;
    }

    /* ---------- infer.run[:<chain_action>] ----------
       Runs inference and checks against threshold.
       If anomaly detected and a chain action is provided,
       that action string is fired (e.g. "log:ANOMALY!").
       Example: infer.run
                infer.run:log:ANOMALY DETECTED MSE=$loss
    -------------------------------------------------- */
    if (!strncmp(a, "infer.run", 9)) {
        float loss = 0.0f;
        bool anomaly = inference_mgr_run_and_check(&loss);

        if (anomaly) {
            /* Check for chained action after "infer.run:" */
            const char *chain = NULL;
            if (a[9] == ':') {
                chain = a + 10;
            }
            if (chain && *chain) {
                /* Build a temporary rule_t with the chain action */
                rule_t chain_rule = *R;
                strncpy(chain_rule.action, chain, sizeof(chain_rule.action) - 1);
                chain_rule.action[sizeof(chain_rule.action) - 1] = '\0';
                fire_action(&chain_rule, (double)loss, ts_ms);
            } else {
                printf("[RULE] %s: ANOMALY DETECTED MSE=%.4f\n", R->name, loss);
                mqtt_pub_event(R->name, "anomaly", (double)loss, "ANOMALY", ts_ms);
            }
        }
        return;
    }

    /* ---------- record.json:<tag>:<var1>,<var2>,... ----------
       Publishes named rule variables as a JSON object to the data collection
       MQTT topic: pico/<node>/data/<tag>
       Example: record.json:accel:m_avg,m_std,m_min,m_max

       Published payload:
         {"ts":12345,"source":"GY511_ACC","m_avg":0.97,"m_std":0.003,...}
    -------------------------------------------------- */
    if (!strncmp(a, "record.json:", 12)) {
        const char *rest = a + 12;

        /* Parse tag (up to next ':') */
        const char *colon = strchr(rest, ':');
        char tag[32] = "collect";
        if (colon) {
            size_t tag_len = (size_t)(colon - rest);
            if (tag_len >= sizeof(tag)) tag_len = sizeof(tag) - 1;
            memcpy(tag, rest, tag_len);
            tag[tag_len] = '\0';
        }

        /* Parse comma-separated variable list after tag */
        const char *var_list = colon ? colon + 1 : "";

        /* Build JSON payload */
        static char json[512];
        int jpos = 0;
        jpos += snprintf(json + jpos, sizeof(json) - (size_t)jpos,
                         "{\"ts\":%u,\"source\":\"%s\"",
                         (unsigned)ts_ms, R->source);

        /* Iterate over variable names */
        char vars_copy[128];
        strncpy(vars_copy, var_list, sizeof(vars_copy) - 1);
        vars_copy[sizeof(vars_copy) - 1] = '\0';

        char *saveptr2;
        char *var = strtok_r(vars_copy, ",", &saveptr2);
        while (var && jpos < (int)sizeof(json) - 32) {
            /* Trim spaces */
            while (*var == ' ' || *var == '\t') var++;
            char *ve = var + strlen(var);
            while (ve > var && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = '\0';

            double v = 0.0;
            if (rules_get_var(R, var, &v)) {
                jpos += snprintf(json + jpos, sizeof(json) - (size_t)jpos,
                                 ",\"%s\":%.6g", var, v);
            }
            var = strtok_r(NULL, ",", &saveptr2);
        }

        /* Close JSON */
        if (jpos < (int)sizeof(json) - 2) {
            json[jpos++] = '}';
            json[jpos]   = '\0';
        }

        /* Publish to pico/<node>/data/<tag> */
        char topic[128];
        snprintf(topic, sizeof(topic), "pico/pico-001/data/%s", tag);
        mqtt_pub_text(topic, "%s", json);

        printf("[RECORD] %s -> %s: %s\n", R->name, topic, json);
        return;
    }

    /* ------------- Fallback: unknown action type ------------- */
    printf("[RULE] %s: action=%s (value=%.3f)\n",
           R->name, a, value);
    mqtt_pub_event(R->name, "other", value, a, ts_ms);
}
