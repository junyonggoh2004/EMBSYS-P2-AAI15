/* D1 task 1.4: the console REPL and the MQTT command topic share one command
 * table, repl_command(). The real repl.c is compiled; the scheduler, rule
 * engine and HAL around it are recorded here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal/hal.h"
#include "bus/bus_common.h"
#include "application/scheduler.h"
#include "rule_engine/rules.h"

static unsigned window_stops, pair_cancels, window_reports, scheduler_resets, rule_resets,
                rule_dumps, scheduler_dumps, cfg_lines, cfg_adds, rule_lines;
static uint32_t report_token;
static char last_args[64];

void scheduler_window_stop(void) { ++window_stops; }
void scheduler_pair_cancel(void) { ++pair_cancels; }
int scheduler_window_report(uint32_t token) { ++window_reports; report_token = token; return HAL_OK; }
int scheduler_window_command(const char *args) { snprintf(last_args, sizeof last_args, "W:%s", args); return HAL_OK; }
int scheduler_pair_command(const char *args) { snprintf(last_args, sizeof last_args, "P:%s", args); return HAL_OK; }
int scheduler_trigger(const char *source, uint64_t t) { (void)t; snprintf(last_args, sizeof last_args, "T:%s", source); return 1; }
void scheduler_reset(void) { ++scheduler_resets; }
void scheduler_dump(void) { ++scheduler_dumps; }
int scheduler_add_config(const app_cfg_t *cfg) { (void)cfg; ++cfg_adds; return 0; }
void rules_reset(void) { ++rule_resets; }
void rules_dump(void) { ++rule_dumps; }
void rules_begin(void) { }
void rules_feed_line(const char *line) { (void)line; ++rule_lines; }
bool rules_end(void) { return true; }
void cfg_parse_begin(void) { }
void cfg_parse_line(const char *line, app_cfg_t *cfg) { (void)line; (void)cfg; ++cfg_lines; }
void cfg_parse_end(app_cfg_t *cfg) { (void)cfg; }
uint64_t hal_time_us(void) { return 1000; }
int hal_console_getc(uint32_t timeout) { (void)timeout; return HAL_ERR_TIMEOUT; }

static void console(const char *text, bool *running, bool *ready)
{
    char line[128];
    snprintf(line, sizeof line, "%s", text);
    repl_handle_line(line, running, ready);
}

int main(void)
{
    bool running = false, ready = false;

    /* RUN and STOP keep their console side effects. */
    assert(repl_command("run", &running, &ready) && running && pair_cancels == 1);
    assert(repl_command("STOP", &running, &ready) && !running && window_stops == 1 && pair_cancels == 2);

    /* Commands with arguments reach the scheduler unchanged. */
    assert(repl_command("WINDOW STATUS 0000abcd", &running, &ready) && window_reports == 1 && report_token == 0xabcd);
    assert(repl_command("WINDOW arm 3", &running, &ready) && !strcmp(last_args, "W:arm 3"));
    assert(repl_command("PAIR A B A 100", &running, &ready) && !strcmp(last_args, "P:A B A 100"));
    assert(repl_command("TRIGGER  SRC", &running, &ready) && !strcmp(last_args, "T:SRC"));

    /* WINDOW and PAIR are refused while sampling, as on the console. */
    running = true;
    last_args[0] = 0;
    assert(repl_command("WINDOW STATUS", &running, &ready) && window_reports == 1);
    assert(repl_command("PAIR A B A 100", &running, &ready) && !last_args[0]);
    running = false;

    assert(repl_command("SHOWRULES", &running, &ready) && rule_dumps == 1);
    assert(repl_command("CLEARRULES", &running, &ready) && rule_resets == 1 && scheduler_resets == 0);
    assert(repl_command("clear", &running, &ready) && rule_resets == 2 && scheduler_resets == 1);
    assert(repl_command("SHOW", &running, &ready) && scheduler_dumps == 1 && rule_dumps == 2);
    assert(repl_command("HELP", &running, &ready));

    /* Configuration blocks and unknown text are not commands. */
    assert(!repl_command("BEGINCFG", &running, &ready));
    assert(!repl_command("BEGINRULE", &running, &ready));
    assert(!repl_command("BEGINCFG|name=X|proto=gpio|ENDCFG", &running, &ready));
    assert(!repl_command("FOO", &running, &ready));
    assert(!repl_command("", &running, &ready));

    /* The console uses the same table. */
    console("SHOWRULES", &running, &ready);
    assert(rule_dumps == 3);

    /* An MQTT command in the middle of a typed config block runs and leaves
     * the block intact: repl_command() does not touch the console's state. */
    console("BEGINCFG", &running, &ready);
    console("name=X", &running, &ready);
    assert(repl_command("SHOWRULES", &running, &ready) && rule_dumps == 4);
    console("proto=gpio", &running, &ready);
    console("ENDCFG", &running, &ready);
    assert(cfg_lines == 2 && cfg_adds == 1 && ready);

    console("BEGINRULE", &running, &ready);
    assert(repl_command("STOP", &running, &ready));
    console("name=R", &running, &ready);
    console("ENDRULE", &running, &ready);
    assert(rule_lines == 1);

    console("FOO", &running, &ready); /* prints "Unknown: FOO" */
    puts("REPL command table: PASS (console and MQTT share repl_command; config blocks unaffected)");
    return 0;
}
