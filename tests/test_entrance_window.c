#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "application/entrance_window.h"
#include "application/event_identity.h"
#include "application/application_state.h"
#include "application/ld2450.h"

typedef struct {
    entrance_window_t window;
    application_state_t app;
    uint64_t now, arm_us[2];
    unsigned mask, emitted, starts, cancelled;
    int owner;
    bool fail, refuse;
    entrance_result_t last;
} fixture_t;
static int start(void *context, unsigned sensor, uint32_t timeout)
{
    fixture_t *f = context;
    assert(f->owner == -1 && timeout == 12000);
    f->owner = (int)sensor; f->arm_us[sensor] = f->now; ++f->starts;
    return HAL_OK;
}
static int poll(void *context, unsigned sensor, hal_pulse_result_t *pulse)
{
    fixture_t *f = context;
    assert(f->owner == (int)sensor);
    if (f->fail && sensor == 0) return HAL_ERR_TIMEOUT;
    uint32_t width = f->mask & (1u << sensor) ? 580 : 5800;
    *pulse = (hal_pulse_result_t){f->arm_us[sensor] + 100, f->arm_us[sensor] + 100 + width, width};
    return HAL_OK;
}
static void cancel(void *context, unsigned sensor)
{
    fixture_t *f = context; assert(f->owner == (int)sensor);
    f->owner = -1; ++f->cancelled;
}
static bool emit(const entrance_result_t *r, void *context)
{
    fixture_t *f = context;
    f->last = *r; ++f->emitted;
    event_record_t event = { .schema_version=EVENT_RECORD_SCHEMA_VERSION, .source_id=1,
        .window_start_us=r->start_us, .window_end_us=r->end_us,
        .quality_flags=r->quality_flags, .valid=1, .rule_class=(uint8_t)r->classification };
    assert(event_identity_next(&event.node_id, &event.boot_id, &event.source_seq));
    event.event_id = event.source_seq;
    if (!f->refuse) {
        application_process_event(&f->app, &event, r->end_us);
        int32_t occupancy = f->app.occupancy;
        application_result_t duplicate = application_process_event(&f->app, &event, r->end_us);
        assert(duplicate.code == ((r->quality_flags & EVENT_QUALITY_STALE) ? APP_RESULT_REJECTED : APP_RESULT_DUPLICATE));
        assert(f->app.occupancy == occupancy);
    }
    return !f->refuse;
}
static void init(fixture_t *f)
{
    memset(f, 0, sizeof *f); f->owner = -1;
    const entrance_config_t config = {.pair={.first=0, .timeout_us={12000,12000},
        .guard_us=1000, .window_us=50000, .error_policy=PULSE_SEQUENCE_COLLECT_BOTH},
        .occupied_mm={500,500}};
    const pulse_sequence_ops_t ops = {start,poll,cancel};
    assert(entrance_window_configure(&f->window, &config, &ops, f, emit, f) == HAL_OK);
    application_state_init(&f->app, NULL);
}
static void tick(fixture_t *f, uint64_t now)
{ f->now = now; entrance_window_step(&f->window, now); }
static void pair(fixture_t *f, uint64_t now, unsigned mask, unsigned radar)
{
    f->mask = mask;
    entrance_window_radar(&f->window, radar, now);
    tick(f, now); tick(f, now+6000); tick(f, now+7000); tick(f, now+13000);
}
static void test_crossings_and_rearm(void)
{
    fixture_t f; init(&f);
    const unsigned entry[] = {0,1,3,2,0,0,0};
    for (unsigned n=0; n<7; ++n) pair(&f,1000000+n*100000,entry[n],1);
    assert(f.emitted == 1 && f.last.classification == EVENT_CLASS_ENTRY && f.app.occupancy == 1);
    assert(f.last.pairs == 7 && f.last.confirmed && f.window.state == ENTRANCE_COOLDOWN);
    for (unsigned n=7; n<11; ++n) pair(&f,1000000+n*100000,0,1);
    assert(f.emitted == 1); // persistent presence never extends/reopens a window
    for (unsigned n=11; n<14; ++n) pair(&f,1000000+n*100000,0,0);
    assert(f.window.state == ENTRANCE_IDLE);
    const unsigned exit[] = {0,2,3,1,0,0,0};
    for (unsigned n=0; n<7; ++n) pair(&f,2400000+n*100000,exit[n],1);
    assert(f.emitted == 2 && f.last.classification == EVENT_CLASS_EXIT && f.app.occupancy == 0);
}
static void test_faults_and_expiry(void)
{
    fixture_t f; init(&f); f.fail = true;
    for (unsigned n=0;n<3;++n) pair(&f,1000000+n*100000,0,1);
    assert(f.emitted == 1 && f.last.reason == ENTRANCE_SENSOR_FAULT && f.last.failed_pairs == 3);
    assert(f.app.occupancy == 0 && f.last.classification == EVENT_CLASS_INCOMPLETE);
    assert(f.starts == 6); // collect both sensors even when first fails

    init(&f); entrance_window_radar(&f.window,1,1000000);
    tick(&f,1300000);
    assert(f.last.reason == ENTRANCE_RADAR_STALE && f.emitted == 1);

    init(&f);
    for (unsigned n=0;n<20;++n) pair(&f,1000000+n*100000,0,1);
    entrance_window_radar(&f.window,1,3000000); tick(&f,3000000);
    assert(f.emitted == 1 && f.last.reason == ENTRANCE_DEADLINE && f.last.pairs == 20);
    assert(f.last.end_us - f.last.start_us == ENTRANCE_WINDOW_US && f.app.occupancy == 0);
}
static void test_ambiguity_loss_and_cancel(void)
{
    const unsigned masks[] = {0,1,3,2,0,0,0};
    fixture_t f; init(&f);
    for (unsigned n=0;n<7;++n) pair(&f,1000000+n*100000,masks[n],n==2?2:1);
    assert(f.emitted == 1 && f.app.occupancy == 0 && f.last.classification == EVENT_CLASS_INCOMPLETE);
    init(&f);
    for (unsigned n=0;n<7;++n) {
        if (n==2) entrance_window_fault(&f.window);
        pair(&f,1000000+n*100000,masks[n],1);
    }
    assert(f.emitted == 1 && f.app.occupancy == 0);
    init(&f); f.refuse = true;
    entrance_window_radar(&f.window,1,1000000); tick(&f,1000000);
    entrance_window_stop(&f.window,1000010);
    entrance_window_stop(&f.window,1000020); tick(&f,2000000);
    assert(f.emitted == 1 && f.window.drops == 1 && f.owner == -1 && !f.window.enabled);
}
static unsigned reports, targets;
static void report(unsigned n,uint64_t now,void *context)
{ (void)context; assert(now==1000000); ++reports; targets=n; }
static void test_decoder(void)
{
    ld2450_decoder_t d={0};
    const ld2450_zone_t zone={-500,500,0,1500};
    uint8_t frame[30]={0xaa,0xff,3,0, 0xc8,0,0xe8,0x83,0,0,0x40,1}; // x=-200,y=1000
    frame[28]=0x55; frame[29]=0xcc;
    for(unsigned i=0;i<30;++i) ld2450_feed(&d,&zone,frame+i,1,1000000,report,NULL);
    assert(reports==1 && targets==1);
    frame[29]=0;
    ld2450_feed(&d,&zone,frame,30,1000000,report,NULL);
    assert(reports==1 && d.malformed==1);
    frame[29]=0xcc;
    ld2450_feed(&d,&zone,frame,30,1000000,report,NULL);
    assert(reports==2 && targets==1);
    memset(frame+4,0,24);
    ld2450_feed(&d,&zone,frame,30,1000000,report,NULL);
    assert(reports==3 && targets==0);
    uint32_t random=12345;
    for(unsigned i=0;i<10000;++i) {
        random=random*1664525u+1013904223u;
        uint8_t byte=(uint8_t)(random>>24);
        ld2450_feed(&d,&zone,&byte,1,1000000,report,NULL);
        assert(d.used < sizeof d.bytes);
    }
}
static void test_confirmation_and_clock(void)
{
    fixture_t f; init(&f);
    pair(&f,1000000,0,1); pair(&f,1100000,0,0); pair(&f,1200000,0,0);
    entrance_window_radar(&f.window,0,1300000); tick(&f,1300000);
    assert(f.emitted==1 && f.last.reason==ENTRANCE_UNCONFIRMED && f.app.occupancy==0);
    init(&f); pair(&f,1000000,0,1);
    entrance_window_radar(&f.window,1,999999);
    assert(!f.window.enabled && f.emitted==1 && f.last.reason==ENTRANCE_CLOCK_ERROR);
    init(&f);
    entrance_window_radar(&f.window,1,1000000);
    pair(&f,1020000,0,1); // service is 20 ms late; later slots keep the original anchor
    assert(f.window.next_pair_us==1100000);
}
int main(void)
{
    assert(event_identity_install(0xcafe,3));
    test_crossings_and_rearm(); test_faults_and_expiry(); test_ambiguity_loss_and_cancel(); test_decoder();
    test_confirmation_and_clock();
    puts("entrance window: PASS (radar framing, real pair engine, window policy, canonical events and occupancy)");
}
