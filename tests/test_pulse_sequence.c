#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "application/pulse_sequence.h"

typedef struct {
    unsigned starts[2], polls[2], cancels[2];
    unsigned order[2], count;
    int owned;
    int start_status[2], poll_status[2];
    hal_pulse_result_t pulse[2];
} fixture_t;

static int start(void *context, unsigned sensor, uint32_t timeout)
{
    fixture_t *f = context;
    assert(sensor < 2 && timeout && f->owned == -1);
    ++f->starts[sensor];
    assert(f->count < 2);
    f->order[f->count++] = sensor;
    if (f->start_status[sensor] == HAL_OK) f->owned = (int)sensor;
    return f->start_status[sensor];
}
static int poll(void *context, unsigned sensor, hal_pulse_result_t *result)
{
    fixture_t *f = context;
    assert(f->owned == (int)sensor);
    ++f->polls[sensor];
    *result = f->pulse[sensor];
    return f->poll_status[sensor];
}
static void cancel(void *context, unsigned sensor)
{
    fixture_t *f = context;
    assert(f->owned == (int)sensor);
    f->owned = -1;
    ++f->cancels[sensor];
}
static const pulse_sequence_ops_t ops = {start, poll, cancel};
static const pulse_sequence_config_t config = {
    .first = 0, .timeout_us = {20000, 20000}, .guard_us = 10000,
    .window_us = 50000, .error_policy = PULSE_SEQUENCE_FAIL_FAST
};

static fixture_t fixture(void)
{
    fixture_t f = {.owned = -1};
    f.poll_status[0] = f.poll_status[1] = HAL_PULSE_PENDING;
    return f;
}

static void test_both_orders(void)
{
    for (unsigned first = 0; first < 2; ++first) {
        fixture_t f = fixture();
        pulse_sequence_t s = {0};
        pulse_sequence_config_t c = config;
        c.first = first;
        unsigned second = 1u - first;
        assert(pulse_sequence_begin(&s, &c, &ops, &f, 1000000) == HAL_OK);
        assert(pulse_sequence_wait_us(&s, 1000000, 100000) == 0);
        assert(pulse_sequence_step(&s, 1000000) == PULSE_SEQUENCE_WAIT);
        assert(pulse_sequence_wait_us(&s, 1000000, 100000) == 1000);
        pulse_sequence_t saved = s;
        assert(pulse_sequence_begin(&s, &c, &ops, &f, 1000001) == HAL_ERR_BUSY);
        assert(!memcmp(&s, &saved, sizeof s));
        assert(pulse_sequence_step(&s, 1001000) == PULSE_SEQUENCE_WAIT);
        assert(f.count == 1 && f.starts[second] == 0);
        f.poll_status[first] = HAL_OK;
        f.pulse[first] = (hal_pulse_result_t){1000100, 1001100, 1000};
        assert(pulse_sequence_step(&s, 1002000) == PULSE_SEQUENCE_GUARD);
        assert(f.owned == -1 && s.samples[first].valid);
        assert(pulse_sequence_wait_us(&s, 1002000, 100000) == 10000);
        assert(pulse_sequence_step(&s, 1011999) == PULSE_SEQUENCE_GUARD);
        assert(f.count == 1);
        assert(pulse_sequence_step(&s, 1012000) == PULSE_SEQUENCE_WAIT);
        assert(f.order[0] == first && f.order[1] == second);
        f.poll_status[second] = HAL_OK;
        f.pulse[second] = (hal_pulse_result_t){1012100, 1013100, 1000};
        assert(pulse_sequence_step(&s, 1014000) == PULSE_SEQUENCE_COMPLETE);
        assert(s.outcome == PULSE_SEQUENCE_OK && s.completed == 2);
        assert(s.samples[0].valid && s.samples[1].valid && f.owned == -1);
        assert(s.finished_us - s.started_us == 14000);
        unsigned total_polls = f.polls[0] + f.polls[1];
        assert(pulse_sequence_step(&s, 2000000) == PULSE_SEQUENCE_COMPLETE);
        pulse_sequence_cancel(&s, 2000000);
        assert(f.polls[0] + f.polls[1] == total_polls && f.count == 2);
    }
}

static void test_timeouts_and_errors(void)
{
    fixture_t f = fixture();
    pulse_sequence_t s = {0};
    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    pulse_sequence_step(&s, 1000000);
    assert(pulse_sequence_step(&s, 1020000) == PULSE_SEQUENCE_FAILED);
    assert(s.outcome == PULSE_SEQUENCE_CAPTURE_ERROR && f.count == 1 && f.owned == -1);
    assert(s.samples[0].status == HAL_ERR_TIMEOUT && !s.samples[0].valid);

    f = fixture();
    f.start_status[0] = HAL_ERR_BUSY;
    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    assert(pulse_sequence_step(&s, 1000000) == PULSE_SEQUENCE_FAILED);
    assert(!f.cancels[0] && !s.samples[0].valid); // never cancel another owner

    f = fixture();
    pulse_sequence_config_t c = config;
    c.error_policy = PULSE_SEQUENCE_COLLECT_BOTH;
    assert(pulse_sequence_begin(&s, &c, &ops, &f, 1000000) == HAL_OK);
    pulse_sequence_step(&s, 1000000);
    assert(pulse_sequence_step(&s, 1020000) == PULSE_SEQUENCE_GUARD);
    assert(pulse_sequence_step(&s, 1029999) == PULSE_SEQUENCE_GUARD);
    assert(pulse_sequence_step(&s, 1030000) == PULSE_SEQUENCE_WAIT);
    f.poll_status[1] = HAL_OK;
    f.pulse[1] = (hal_pulse_result_t){1030100, 1031100, 1000};
    assert(pulse_sequence_step(&s, 1032000) == PULSE_SEQUENCE_COMPLETE);
    assert(s.outcome == PULSE_SEQUENCE_CAPTURE_ERROR);
    assert(!s.samples[0].valid && s.samples[1].valid);
}

static void test_window_clock_and_cancel(void)
{
    fixture_t f = fixture();
    pulse_sequence_t s = {0};
    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    pulse_sequence_step(&s, 1000000);
    assert(pulse_sequence_step(&s, 1050001) == PULSE_SEQUENCE_FAILED);
    assert(s.outcome == PULSE_SEQUENCE_WINDOW_EXPIRED && !s.samples[0].valid);
    assert(f.cancels[0] == 1 && f.count == 1);

    f = fixture();
    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    pulse_sequence_step(&s, 1000000);
    assert(pulse_sequence_wait_us(&s, 999999, 1000) == 0);
    assert(pulse_sequence_step(&s, 999999) == PULSE_SEQUENCE_FAILED);
    assert(s.outcome == PULSE_SEQUENCE_CLOCK_ERROR && f.owned == -1);

    for (unsigned phase = 0; phase < 3; ++phase) {
        f = fixture();
        assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
        if (phase) pulse_sequence_step(&s, 1000000);
        if (phase == 2) {
            f.poll_status[0] = HAL_OK;
            f.pulse[0] = (hal_pulse_result_t){1000100, 1001100, 1000};
            assert(pulse_sequence_step(&s, 1002000) == PULSE_SEQUENCE_GUARD);
        }
        pulse_sequence_cancel(&s, 1003000);
        assert(s.state == PULSE_SEQUENCE_CANCELLED && f.owned == -1);
        unsigned cancelled = f.cancels[0];
        pulse_sequence_cancel(&s, 1004000);
        pulse_sequence_step(&s, 1005000);
        assert(f.cancels[0] == cancelled && f.starts[1] == 0);
    }
}

static void test_validation_and_late_service(void)
{
    fixture_t f = fixture();
    pulse_sequence_t s = {0}, saved = s;
    pulse_sequence_config_t c = config;
    c.guard_us = 10001;
    assert(pulse_sequence_begin(&s, &c, &ops, &f, 1) == HAL_ERR_INVALID);
    c = config; c.window_us = 50001;
    assert(pulse_sequence_begin(&s, &c, &ops, &f, 1) == HAL_ERR_INVALID);
    c = config; c.timeout_us[0] = UINT32_MAX;
    assert(pulse_sequence_begin(&s, &c, &ops, &f, 1) == HAL_ERR_INVALID);
    c = config; c.first = 2;
    assert(pulse_sequence_begin(&s, &c, &ops, &f, 1) == HAL_ERR_INVALID);
    assert(pulse_sequence_begin(&s, &config, &ops, &f, UINT64_MAX - 49999) == HAL_ERR_INVALID);
    assert(!memcmp(&s, &saved, sizeof s) && f.count == 0);

    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    // Late start cannot fit its full 20 ms timeout inside the 50 ms window.
    assert(pulse_sequence_step(&s, 1030001) == PULSE_SEQUENCE_FAILED);
    assert(s.outcome == PULSE_SEQUENCE_WINDOW_EXPIRED && f.count == 0);

    // Capture completed in time but late service cannot extend its deadline.
    f = fixture();
    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    pulse_sequence_step(&s, 1000000);
    f.poll_status[0] = HAL_OK;
    f.pulse[0] = (hal_pulse_result_t){1000100, 1001100, 1000};
    assert(pulse_sequence_step(&s, 1020001) == PULSE_SEQUENCE_FAILED);
    assert(s.samples[0].status == HAL_ERR_TIMEOUT && !s.samples[0].valid);
}

static void test_bad_timestamps(void)
{
    const hal_pulse_result_t bad[] = {
        {999999, 1000100, 101}, {1000100, 1000100, 0},
        {1000100, 1001100, 999}, {1000100, 1003000, 2900}
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        fixture_t f = fixture();
        pulse_sequence_t s = {0};
        assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
        pulse_sequence_step(&s, 1000000);
        f.poll_status[0] = HAL_OK;
        f.pulse[0] = bad[i];
        assert(pulse_sequence_step(&s, 1002000) == PULSE_SEQUENCE_FAILED);
        assert(s.samples[0].status == HAL_ERR_IO && !s.samples[0].valid);
        assert(!s.samples[0].pulse.width_us && f.owned == -1);
    }
}

static void test_exact_pair_deadline(void)
{
    fixture_t f = fixture();
    pulse_sequence_t s = {0};
    assert(pulse_sequence_begin(&s, &config, &ops, &f, 1000000) == HAL_OK);
    pulse_sequence_step(&s, 1000000);
    f.poll_status[0] = HAL_OK;
    f.pulse[0] = (hal_pulse_result_t){1000100, 1020000, 19900};
    assert(pulse_sequence_step(&s, 1020000) == PULSE_SEQUENCE_GUARD);
    assert(pulse_sequence_step(&s, 1030000) == PULSE_SEQUENCE_WAIT);
    f.poll_status[1] = HAL_OK;
    f.pulse[1] = (hal_pulse_result_t){1030100, 1050000, 19900};
    assert(pulse_sequence_step(&s, 1050000) == PULSE_SEQUENCE_COMPLETE);
    assert(s.finished_us - s.started_us == PULSE_SEQUENCE_WINDOW_MAX_US);
    assert(s.outcome == PULSE_SEQUENCE_OK && s.samples[1].valid);
}

int main(void)
{
    test_both_orders();
    test_timeouts_and_errors();
    test_window_clock_and_cancel();
    test_validation_and_late_service();
    test_bad_timestamps();
    test_exact_pair_deadline();
    printf("pulse sequence: PASS (both orders, guard, 50 ms pair budget, faults/cancel; state=%zu B)\n",
           sizeof(pulse_sequence_t));
}
