#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "application/application_state.h"

static event_record_t entry(uint32_t id, uint32_t source, uint32_t seq)
{
    event_record_t e = {0};
    e.schema_version = EVENT_RECORD_SCHEMA_VERSION;
    e.event_id = id;
    e.source_id = source;
    e.source_seq = seq;
    e.window_start_us = 1000000;
    e.window_end_us = 1001000;
    e.quality_flags = EVENT_QUALITY_SEQUENCE_COMPLETE | EVENT_QUALITY_TIMING_VALID |
                      EVENT_QUALITY_RULE_VALID;
    e.valid = 1;
    e.rule_class = EVENT_CLASS_ENTRY;
    return e;
}

static void test_nonfinite_and_malformed(void)
{
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    const char *reason;
    for (size_t k = 0; k < sizeof invalid / sizeof invalid[0]; ++k) {
        event_record_t e = entry(1, 1, 1);
        e.ml_confidence = invalid[k];
        assert(!event_record_validate(&e, 1002000, 5000, &reason));
        for (size_t i = 0; i < EVENT_FEATURE_COUNT; ++i) {
            e = entry(1, 1, 1);
            e.features[i] = invalid[k];
            assert(!event_record_validate(&e, 1002000, 5000, &reason));
        }
    }
    event_record_t e = entry(1, 1, 1);
    e.valid = 2;
    assert(!event_record_validate(&e, 1002000, 5000, &reason));
    e = entry(1, 1, 1);
    e.reserved = 1;
    assert(!event_record_validate(&e, 1002000, 5000, &reason));
    e = entry(1, 1, 1);
    assert(event_record_validate(&e, 1002000, 5000, &reason));
    e.ml_confidence = 1.0f;
    e.features[0] = -123.5f; /* finite negative features are legitimate */
    assert(event_record_validate(&e, 1002000, 5000, &reason));
}

static void test_quality_does_not_change_occupancy(void)
{
    const uint32_t flags[] = {EVENT_QUALITY_STALE, EVENT_QUALITY_AMBIGUOUS,
                              EVENT_QUALITY_SENSOR_TIMEOUT, EVENT_QUALITY_SENSOR_FAULT};
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; ++i) {
        application_state_t s;
        application_state_init(&s, NULL);
        s.occupancy = 3;
        event_record_t e = entry(1, 1, 1);
        e.quality_flags |= flags[i];
        application_result_t r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_REJECTED && r.degraded && s.occupancy == 3);
        e = entry(2, 1, 2);
        e.rule_class = EVENT_CLASS_EXIT;
        e.quality_flags |= flags[i];
        r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_REJECTED && r.degraded && s.occupancy == 3);
    }
    const uint32_t required[] = {EVENT_QUALITY_SEQUENCE_COMPLETE, EVENT_QUALITY_TIMING_VALID};
    for (size_t i = 0; i < sizeof required / sizeof required[0]; ++i) {
        application_state_t s;
        application_state_init(&s, NULL);
        event_record_t e = entry(1, 1, 1);
        e.quality_flags &= ~required[i];
        application_result_t r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_REJECTED && r.degraded && s.occupancy == 0);
    }
}

static void test_source_scoped_deduplication(void)
{
    application_state_t s;
    application_state_init(&s, NULL);
    for (uint32_t source = 1; source <= APPLICATION_SOURCE_HISTORY; ++source) {
        event_record_t e = entry(1, source, 1);
        application_result_t r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_APPLIED && s.occupancy == (int32_t)source);
        r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_DUPLICATE && s.occupancy == (int32_t)source);
    }
    event_record_t e = entry(1, APPLICATION_SOURCE_HISTORY + 1, 1);
    application_result_t r = application_process_event(&s, &e, 1002000);
    assert(r.code == APP_RESULT_REJECTED && s.occupancy == APPLICATION_SOURCE_HISTORY);
    assert(!strcmp(r.reason, "source capacity"));

    application_state_init(&s, NULL);
    for (uint32_t i = 1; i <= APPLICATION_DEDUP_HISTORY + 8; ++i) {
        e = entry(i, 1, i);
        r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_APPLIED);
    }
    assert(s.recent_count == APPLICATION_DEDUP_HISTORY);
    int32_t before = s.occupancy;
    e = entry(1, 1, 1); /* evicted ID is still caught by source ordering */
    r = application_process_event(&s, &e, 1002000);
    assert(r.code == APP_RESULT_REORDERED && s.occupancy == before);
    e = entry(1, 2, 1); /* another source is independent even after ring wrap */
    r = application_process_event(&s, &e, 1002000);
    assert(r.code == APP_RESULT_APPLIED && s.occupancy == before + 1);
}

static void test_policy_and_occupancy_bounds(void)
{
    application_state_t s;
    event_record_t e = entry(1, 1, 1);
    const float thresholds[] = {NAN, INFINITY, -INFINITY, -0.1f, 1.1f};
    application_result_t r;
    for (size_t i = 0; i < sizeof thresholds / sizeof thresholds[0]; ++i) {
        application_policy_t policy = {5000, thresholds[i], true, false};
        application_state_init(&s, &policy);
        r = application_process_event(&s, &e, 1002000);
        assert(r.code == APP_RESULT_REJECTED && r.degraded && s.occupancy == 0);
        assert(!strcmp(r.reason, "policy"));
    }

    application_state_init(&s, NULL);
    s.occupancy = INT32_MAX;
    r = application_process_event(&s, &e, 1002000);
    assert(r.code == APP_RESULT_REJECTED && r.degraded && s.occupancy == INT32_MAX);
    assert(!strcmp(r.reason, "occupancy capacity"));
    assert(s.accepted == 0 && s.rejected == 1);
    e = entry(2, 1, 2);
    e.rule_class = EVENT_CLASS_EXIT;
    r = application_process_event(&s, &e, 1002000);
    assert(r.code == APP_RESULT_APPLIED && s.occupancy == INT32_MAX - 1);
}

static void test_canonical_identity_and_clocks(void)
{
    application_state_t s;
    application_state_init(&s, NULL);
    event_record_t e = entry(1, 1, 1);
    e.node_id = 0x1234; e.boot_id = 1;
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_APPLIED);
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_DUPLICATE);
    e.node_id = 0x5678;
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_APPLIED);
    e.node_id = 0x1234; e.boot_id = 2;
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_APPLIED);
    e.boot_id = 1; e.event_id = e.source_seq = 2;
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_REORDERED);
    e.boot_id = 3; e.quality_flags |= EVENT_QUALITY_AMBIGUOUS;
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_REJECTED);
    e.boot_id = 2; e.event_id = e.source_seq = 3;
    e.quality_flags &= ~EVENT_QUALITY_AMBIGUOUS;
    assert(application_process_event(&s,&e,1002000).code == APP_RESULT_REORDERED);
    e.boot_id = 4; e.clock_domain = 1;
    const char *reason;
    assert(!event_record_validate(&e,1002000,5000,&reason) && !strcmp(reason,"unmapped clock"));
    e.clock_domain = 0;
    assert(!event_record_validate(&e,1000000,5000,&reason) && !strcmp(reason,"future"));
    e.boot_id = 0;
    assert(!event_record_validate(&e,1002000,5000,&reason));
}

int main(void)
{
    test_nonfinite_and_malformed();
    test_quality_does_not_change_occupancy();
    test_source_scoped_deduplication();
    test_policy_and_occupancy_bounds();
    test_canonical_identity_and_clocks();
    assert(sizeof(event_record_t) == 96);
    printf("event safety: PASS (finite fields, quality rejection, source-scoped duplicates, occupancy bounds; state=%zu B)\n",
           sizeof(application_state_t));
    return 0;
}
