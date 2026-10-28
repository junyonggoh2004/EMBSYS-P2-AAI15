#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "application/application_state.h"
#include "application/timing_contract.h"
#include "bus/bus_common.h"

bool hal_gpio_valid(int pin) { return pin >= 0 && pin < 49; }

static event_record_t captured;
static bool capture_sink(const event_record_t *item) {
    captured = *item;
    return true;
}

static event_record_t event(uint32_t id, uint32_t seq, event_class_t klass,
                            uint64_t end_us) {
    event_record_t e;
    memset(&e, 0, sizeof e);
    e.schema_version = EVENT_RECORD_SCHEMA_VERSION;
    e.event_id = id;
    e.source_id = 7;
    e.source_seq = seq;
    e.window_start_us = end_us - 1000u;
    e.window_end_us = end_us;
    e.quality_flags = EVENT_QUALITY_SEQUENCE_COMPLETE |
                      EVENT_QUALITY_TIMING_VALID |
                      EVENT_QUALITY_RULE_VALID;
    e.valid = 1;
    e.rule_class = (uint8_t)klass;
    return e;
}

static void test_timing(void) {
    const char *reason = NULL;
    timing_config_t periodic = { TIMING_MODE_PERIODIC, 50, 10, 0, true };
    assert(timing_config_validate(&periodic, &reason));
    timing_state_t s;
    timing_state_init(&s, &periodic, 1000000, false);
    assert(timing_state_poll(&s, 1049999) == TIMING_STATUS_ARMED);
    assert(timing_state_poll(&s, 1050000) == TIMING_STATUS_DUE);
    timing_state_complete(&s, 1051000);
    assert(s.due_us == 1100000);

    timing_config_t one = { TIMING_MODE_ONE_SHOT, 0, 20, 0, true };
    assert(timing_config_validate(&one, &reason));
    timing_state_init(&s, &one, 2000000, true);
    assert(timing_state_poll(&s, 2000000) == TIMING_STATUS_DUE);
    timing_state_complete(&s, 2000100);
    assert(timing_state_poll(&s, 3000000) == TIMING_STATUS_COMPLETE);

    timing_config_t driven = { TIMING_MODE_EVENT_DRIVEN, 0, 20, 50, true };
    assert(timing_config_validate(&driven, &reason));
    timing_state_init(&s, &driven, 0, false);
    assert(s.status == TIMING_STATUS_IDLE);
    assert(timing_state_trigger(&s, 3000000));
    assert(!timing_state_trigger(&s, 3000001));
    assert(timing_state_poll(&s, 3000000) == TIMING_STATUS_DUE);
    assert(s.deadline_us == 3020000); /* timeout 20ms wins over 50ms window */
    assert(timing_state_poll(&s, 3020000) == TIMING_STATUS_DUE);
    assert(timing_state_poll(&s, 3020001) == TIMING_STATUS_TIMEOUT);
    assert(s.timeouts == 1);
    assert(timing_state_poll(&s, 3020002) == TIMING_STATUS_TIMEOUT);
    assert(s.timeouts == 1); /* only count the transition once */
    timing_state_complete(&s, 3020002);
    assert(s.status == TIMING_STATUS_COMPLETE && s.executions == 0);
    assert(timing_state_trigger(&s, 4000000));
    assert(timing_state_poll(&s, 4000000) == TIMING_STATUS_DUE);
    timing_state_complete(&s, 4020000);
    assert(s.status == TIMING_STATUS_COMPLETE && s.executions == 1);

    driven.timeout_ms = 0;
    timing_state_init(&s, &driven, 0, false);
    assert(timing_state_trigger(&s, 5000000));
    assert(s.deadline_us == 5050000); /* window-only bound */

    periodic.interval_ms = 0;
    assert(!timing_config_validate(&periodic, &reason));
    timing_state_init(&s, &periodic, 6000000, true);
    assert(s.status == TIMING_STATUS_DISABLED);
    assert(!timing_state_trigger(&s, 6000000));
    timing_state_complete(&s, 6000000); /* no divide by zero on invalid state */
    assert(s.status == TIMING_STATUS_DISABLED);
}

static void test_application(void) {
    application_state_t state;
    application_state_init(&state, NULL);

    event_record_t e1 = event(1, 1, EVENT_CLASS_ENTRY, 1000000);
    application_result_t r = application_process_event(&state, &e1, 1001000);
    assert(r.code == APP_RESULT_APPLIED && state.occupancy == 1);
    r = application_process_event(&state, &e1, 1002000);
    assert(r.code == APP_RESULT_DUPLICATE && state.occupancy == 1);

    event_record_t e2 = event(2, 2, EVENT_CLASS_EXIT, 1100000);
    r = application_process_event(&state, &e2, 1101000);
    assert(r.code == APP_RESULT_APPLIED && state.occupancy == 0);
    event_record_t e3 = event(3, 3, EVENT_CLASS_EXIT, 1200000);
    r = application_process_event(&state, &e3, 1201000);
    assert(r.code == APP_RESULT_NO_CHANGE && state.occupancy == 0);

    event_record_t incomplete = event(4, 4, EVENT_CLASS_INCOMPLETE, 1300000);
    r = application_process_event(&state, &incomplete, 1301000);
    assert(r.code == APP_RESULT_NO_CHANGE && state.occupancy == 0);
    event_record_t false_event = event(5, 5, EVENT_CLASS_FALSE, 1400000);
    r = application_process_event(&state, &false_event, 1401000);
    assert(r.code == APP_RESULT_NO_CHANGE && state.occupancy == 0);

    event_record_t reordered = event(6, 4, EVENT_CLASS_ENTRY, 1350000);
    r = application_process_event(&state, &reordered, 1402000);
    assert(r.code == APP_RESULT_REORDERED && state.occupancy == 0);
    event_record_t stale = event(7, 6, EVENT_CLASS_ENTRY, 1500000);
    r = application_process_event(&state, &stale, 7000000);
    assert(r.code == APP_RESULT_REJECTED && state.occupancy == 0);

    event_record_t ml = event(8, 6, EVENT_CLASS_EXIT, 7100000);
    ml.quality_flags |= EVENT_QUALITY_ML_VALID;
    ml.ml_class = EVENT_CLASS_ENTRY;
    ml.ml_confidence = 0.90f;
    r = application_process_event(&state, &ml, 7101000);
    assert(r.decision_source == APP_DECISION_ML && state.occupancy == 1);
}

static void test_config_and_sink(void) {
    app_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.proto = proto_gpio;
    cfg.mode = sample_mode_poll;
    cfg.timing.mode = TIMING_MODE_PERIODIC;
    cfg.timing.interval_ms = 1000;
    cfg.timing.enabled = true;
    cfg_parse_begin();
    cfg_parse_line("timing.mode=event_driven", &cfg);
    cfg_parse_line("timing.window_ms=50", &cfg);
    cfg_parse_line("timing.timeout_ms=20", &cfg);
    cfg_parse_line("timing.trigger_source=RADAR", &cfg);
    cfg_parse_end(&cfg);
    assert(!cfg.cfg_error[0]);
    assert(cfg.timing.mode == TIMING_MODE_EVENT_DRIVEN);
    assert(cfg.timing.window_ms == 50 && cfg.timing.timeout_ms == 20);

    app_cfg_t bad = cfg;
    bad.cfg_error[0] = 0;
    cfg_parse_begin();
    cfg_parse_line("timing.timeout_ms=60", &bad);
    cfg_parse_end(&bad);
    assert(bad.cfg_error[0]);

    event_record_set_sink(capture_sink);
    event_record_t sent = event(event_record_next_id(), 10, EVENT_CLASS_ENTRY, 9000000);
    sent.source_id = event_source_id("DOOR_A");
    assert(event_record_submit(&sent));
    assert(captured.event_id == sent.event_id && captured.source_id == sent.source_id);
}

int main(void) {
    test_timing();
    test_application();
    test_config_and_sink();
    printf("cody contracts: PASS (EventRecord=%zu bytes)\n", sizeof(event_record_t));
    return 0;
}
