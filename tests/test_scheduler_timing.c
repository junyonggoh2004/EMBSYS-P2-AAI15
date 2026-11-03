#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application/scheduler.h"
#include "bus/bus_if.h"
#include "mqtt/mqtt_telemetry.h"
#include "rule_engine/rules.h"

/* Exercise the real scheduler, timing state and output publisher together.
 * Only the hardware bus, clock, rule consumer and network are fakes. */
typedef struct {
    char name[16];
    uint32_t delay_us;
    unsigned polls, starts, stops;
    unsigned pending_polls, cancels;
} fixture_t;

static uint64_t now_us;
static unsigned samples;
static fixture_t *fixtures[8];
static size_t fixture_count;

uint64_t hal_time_us(void) { return now_us; }
uint32_t hal_time_ms(void) { return (uint32_t)(now_us / 1000); }

static int init(const sensor_cfg_t *sensor, void **out)
{
    fixture_t *f = calloc(1, sizeof *f);
    assert(f && fixture_count < 8);
    snprintf(f->name, sizeof f->name, "%s", sensor->name);
    fixtures[fixture_count++] = f;
    *out = f;
    return 0;
}
static void deinit(void *context) { free(context); }
static int poll(void *context, uint8_t *buf, size_t cap)
{
    fixture_t *f = context;
    assert(cap);
    ++f->polls;
    if (f->pending_polls) {
        --f->pending_polls;
        return BUS_POLL_PENDING;
    }
    now_us += f->delay_us;
    buf[0] = 1;
    return 1;
}
static void start(void *context) { ++((fixture_t *)context)->starts; }
static void stop(void *context) { ++((fixture_t *)context)->stops; }
static void cancel(void *context) { ++((fixture_t *)context)->cancels; }
static const bus_t bus = {
    .name = "gpio", .init_cfg = init, .deinit = deinit, .poll_once = poll,
    .stream_start = start, .stream_stop = stop, .cancel = cancel
};
const bus_t *bus_lookup(const char *name) { (void)name; return &bus; }
bool mqtt_follow_remote_source(const char *node, const char *source, const char *alias)
{ (void)node; (void)source; (void)alias; return true; }
bool mqtt_pub_bytes(const char *source, const uint8_t *data, size_t len, int qos, bool retain)
{ (void)source; (void)data; (void)len; (void)qos; (void)retain; return true; }
bool mqtt_pub_line(const char *source, const char *line, int qos, bool retain)
{ (void)source; (void)line; (void)qos; (void)retain; return true; }
void rules_on_sample(const char *source, const uint8_t *data, int len, uint32_t ts)
{ (void)source; (void)data; (void)len; (void)ts; ++samples; }

static void reset(void)
{
    scheduler_run_all(false);
    scheduler_reset();
    fixture_count = 0;
    samples = 0;
    now_us = 1000000;
}
static app_cfg_t config(const char *name, timing_mode_t mode)
{
    app_cfg_t c = {0};
    snprintf(c.name, sizeof c.name, "%s", name);
    c.proto = proto_gpio;
    c.mode = sample_mode_poll;
    c.timing = (timing_config_t){mode, 50, 20, 0, true};
    return c;
}

static void test_one_shot(void)
{
    reset();
    app_cfg_t c = config("ONCE", TIMING_MODE_ONE_SHOT);
    assert(scheduler_add_config(&c) == 0);
    scheduler_run_all(true);
    now_us += 1000;
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 1 && samples == 1);
    assert(scheduler_next_due_ms(true, 1000) == 1000);
    assert(scheduler_trigger("ONCE", now_us) == 1);
    scheduler_run_all(true);
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 2 && samples == 2);
}

static void test_periodic_timeout_recovers(void)
{
    reset();
    app_cfg_t c = config("PERIODIC", TIMING_MODE_PERIODIC);
    c.timing.timeout_ms = 10;
    assert(scheduler_add_config(&c) == 0);
    scheduler_run_all(true);
    now_us = 1061000; /* second due time 1050000, deadline 1060000 */
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 1 && samples == 1);
    assert(scheduler_next_due_ms(true, 1000) == 39);
    now_us = 1100000;
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 2 && samples == 2);
}

static void test_disabled_stream(void)
{
    reset();
    app_cfg_t c = config("DISABLED", TIMING_MODE_PERIODIC);
    c.mode = sample_mode_stream;
    c.timing.enabled = false;
    assert(scheduler_add_config(&c) == 0);
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 0 && fixtures[0]->starts == 0 && samples == 0);
    assert(scheduler_next_due_ms(true, 1000) == 1000);
}

static void test_acquisition_deadline(void)
{
    reset();
    app_cfg_t c = config("SLOW", TIMING_MODE_PERIODIC);
    c.timing.timeout_ms = 10;
    assert(scheduler_add_config(&c) == 0);
    fixtures[0]->delay_us = 10001;
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 1 && samples == 0);
    assert(scheduler_next_due_ms(true, 1000) == 40);
    fixtures[0]->delay_us = 10000; /* completion exactly on deadline is valid */
    now_us = 1050000;
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 2 && samples == 1);
}

static void test_event_source_and_timeout(void)
{
    reset();
    app_cfg_t source = config("SOURCE", TIMING_MODE_ONE_SHOT);
    app_cfg_t target = config("TARGET", TIMING_MODE_EVENT_DRIVEN);
    target.timing.window_ms = 50;
    snprintf(target.timing_trigger_source, sizeof target.timing_trigger_source, "SOURCE");
    assert(scheduler_add_config(&source) == 0);
    assert(scheduler_add_config(&target) == 1);
    scheduler_run_all(true);
    now_us += 1000;
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 1 && fixtures[1]->polls == 1 && samples == 2);
    assert(scheduler_trigger("TARGET", now_us) == 1);
    now_us += 21000; /* configured 20ms timeout, inside the larger 50ms window */
    scheduler_run_all(true);
    assert(fixtures[1]->polls == 1 && samples == 2);
    assert(scheduler_trigger("TARGET", now_us) == 1);
    scheduler_run_all(true);
    assert(fixtures[1]->polls == 2 && samples == 3);
}

static void test_clear_while_running(void)
{
    reset();
    app_cfg_t c = config("ONCE", TIMING_MODE_ONE_SHOT);
    assert(scheduler_add_config(&c) == 0);
    scheduler_run_all(true);
    assert(samples == 1);
    scheduler_reset();
    fixture_count = 0;
    assert(scheduler_add_config(&c) == 0);
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 1 && samples == 2);
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 1 && samples == 2);
}

static void test_periodic_skip_and_invalid_config(void)
{
    reset();
    app_cfg_t c = config("PERIODIC", TIMING_MODE_PERIODIC);
    c.timing.timeout_ms = 0;
    assert(scheduler_add_config(&c) == 0);
    scheduler_run_all(true);
    now_us += 151000;
    scheduler_run_all(true);
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 2 && samples == 2); /* no catch-up burst */
    assert(scheduler_next_due_ms(true, 1000) == 49);
    c.timing.interval_ms = 0;
    assert(scheduler_add_config(&c) < 0);
    assert(scheduler_add_config(NULL) < 0);
    assert(fixture_count == 1); /* invalid config did not touch hardware */
}

static void test_pending_acquisition(void)
{
    reset();
    app_cfg_t sonar = config("SONAR", TIMING_MODE_ONE_SHOT);
    app_cfg_t other = config("OTHER", TIMING_MODE_PERIODIC);
    assert(scheduler_add_config(&sonar) == 0);
    assert(scheduler_add_config(&other) == 1);
    fixtures[0]->pending_polls = 2;
    uint64_t before = now_us;
    scheduler_run_all(true);
    assert(now_us == before && samples == 1); // other sensor still serviced
    assert(scheduler_next_due_ms(true, 1000) == 1); // no due-state busy loop
    assert(scheduler_trigger("SONAR", now_us) == 0); // no second arm
    now_us += 1000;
    scheduler_run_all(true);
    assert(samples == 1);
    now_us += 1000;
    scheduler_run_all(true);
    assert(samples == 2 && fixtures[0]->polls == 3);
    scheduler_run_all(true);
    assert(fixtures[0]->polls == 3); // completed one-shot remains complete

    assert(scheduler_trigger("SONAR", now_us) == 1);
    fixtures[0]->pending_polls = 100;
    scheduler_run_all(true);
    now_us += 20001;
    scheduler_run_all(true);
    assert(fixtures[0]->cancels == 1 && samples == 2); // deadline cancels
    assert(scheduler_trigger("SONAR", now_us) == 1);
    scheduler_run_all(true);
    scheduler_run_all(false);
    assert(fixtures[0]->cancels == 2); // STOP cancels exactly once
    scheduler_run_all(false);
    assert(fixtures[0]->cancels == 2);
}

int main(void)
{
    test_one_shot();
    test_periodic_timeout_recovers();
    test_disabled_stream();
    test_acquisition_deadline();
    test_event_source_and_timeout();
    test_clear_while_running();
    test_periodic_skip_and_invalid_config();
    test_pending_acquisition();
    reset();
    puts("scheduler timing: PASS (one-shot, timeout recovery, disabled stream, deadline, source handoff)");
    return 0;
}
