#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "application/application_dispatch.h"
#include "hal/hal.h"
#include "inference_engine/inference_manager.h"
#include "mqtt/mqtt_telemetry.h"
#include "rule_engine/rule_action.h"

static uint64_t now_us = UINT64_C(5000000000000); /* beyond 32-bit ms rollover */
static event_record_t captured;
static unsigned offers;
static bool queue_accepts;
static rule_t rule;

uint64_t hal_time_us(void) { return now_us; }
int hal_gpio_mode(int pin, hal_gpio_mode_t mode)
{ (void)pin; (void)mode; assert(!"unexpected GPIO action"); return 0; }
int hal_gpio_write(int pin, int level)
{ (void)pin; (void)level; assert(!"unexpected GPIO action"); return 0; }
int hal_gpio_toggle(int pin)
{ (void)pin; assert(!"unexpected GPIO action"); return 0; }
int hal_gpio_pulse(int pin, uint32_t ms)
{ (void)pin; (void)ms; assert(!"unexpected GPIO action"); return 0; }
int hal_pwm_set(int pin, float duty)
{ (void)pin; (void)duty; assert(!"unexpected PWM action"); return 0; }
int hal_pwm_stop(int pin)
{ (void)pin; assert(!"unexpected PWM action"); return 0; }
bool rules_get_var(const rule_t *r, const char *name, double *out)
{ (void)r; (void)name; (void)out; assert(!"unexpected variable lookup"); return false; }
void inference_mgr_set_input_slot(int index, float value)
{ (void)index; (void)value; assert(!"unexpected inference action"); }
bool inference_mgr_run_and_check(float *loss)
{ (void)loss; assert(!"unexpected inference action"); return false; }
bool mqtt_pub_text(const char *topic, const char *format, ...)
{ (void)topic; (void)format; assert(!"unexpected direct MQTT action"); return false; }
bool mqtt_pub_event(const char *name, const char *type, double value,
                    const char *message, uint32_t ts)
{ (void)name; (void)type; (void)value; (void)message; (void)ts;
  assert(!"unexpected direct MQTT action"); return false; }

static bool capture(const event_record_t *event)
{
    ++offers;
    if (!queue_accepts) return false;
    captured = *event;
    return true;
}

static application_dispatch_report_t consume(const event_record_t *event)
{
    application_message_t message = {.kind = APPLICATION_MESSAGE_EVENT, .event = *event};
    application_dispatch_report_t report;
    assert(application_dispatch_handle(&message, sizeof message, now_us,
                                        NULL, NULL, &report));
    return report;
}

int main(void)
{
    snprintf(rule.name, sizeof rule.name, "DOOR");
    snprintf(rule.source, sizeof rule.source, "SONAR_A");
    snprintf(rule.action, sizeof rule.action, "event:entry");
    event_record_set_sink(capture);
    application_dispatch_init();
    queue_accepts = true;

    /* A persistent true rule cannot manufacture a completed entrance window
     * and increment occupancy on every raw sensor sample. */
    for (unsigned i = 0; i < 10; ++i) {
        fire_action(&rule, 42.0, 5); /* legacy frame ts is not a 64-bit window */
        assert(captured.quality_flags == EVENT_QUALITY_RULE_VALID);
        assert(captured.window_start_us == now_us && captured.window_end_us == now_us);
        assert(captured.features[0] == 42.0f && captured.rule_class == EVENT_CLASS_ENTRY);
        application_dispatch_report_t report = consume(&captured);
        assert(report.result.code == APP_RESULT_REJECTED && report.result.degraded);
        assert(report.occupancy == 0 && !strcmp(report.result.reason, "quality"));
        now_us += 1000;
    }
    assert(offers == 10);

    snprintf(rule.action, sizeof rule.action, "event:exit");
    fire_action(&rule, 1.0, 5);
    assert(consume(&captured).result.code == APP_RESULT_REJECTED);

    event_record_t previous = captured;
    queue_accepts = false;
    unsigned before = offers;
    fire_action(&rule, 1.0, 5);
    assert(offers == before + 1); /* a full sink is offered once, no retry */
    assert(!memcmp(&previous, &captured, sizeof captured));

    queue_accepts = true;
    snprintf(rule.action, sizeof rule.action, "event:unknown");
    before = offers;
    fire_action(&rule, 1.0, 5);
    assert(offers == before);

    const char *no_change[] = {"event:incomplete", "event:false"};
    for (size_t i = 0; i < sizeof no_change / sizeof no_change[0]; ++i) {
        snprintf(rule.action, sizeof rule.action, "%s", no_change[i]);
        fire_action(&rule, 1.0, 5);
        assert(captured.quality_flags == EVENT_QUALITY_RULE_VALID);
        application_dispatch_report_t report = consume(&captured);
        assert(report.result.code == APP_RESULT_NO_CHANGE && report.occupancy == 0);
    }

    /* An upstream window producer can still submit its own complete record.
     * This manually assembled fixture does not constitute physical evidence. */
    event_record_t completed = captured;
    completed.event_id = event_record_next_id();
    completed.source_id = 2;
    completed.source_seq = 1;
    completed.window_start_us = now_us - 10000;
    completed.rule_class = EVENT_CLASS_ENTRY;
    completed.quality_flags |= EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID;
    assert(event_record_submit(&completed));
    assert(consume(&captured).result.code == APP_RESULT_APPLIED);

    /* The explicitly synthetic console fixture remains useful for plumbing. */
    application_message_t synthetic;
    assert(application_command_parse("APP EVENT 1000 1000 entry", now_us, &synthetic));
    application_dispatch_report_t report = consume(&synthetic.event);
    assert(report.result.code == APP_RESULT_APPLIED && report.occupancy == 2);
    puts("rule provenance: PASS (no invented window quality, 64-bit decision time, bounded sink)");
    return 0;
}
