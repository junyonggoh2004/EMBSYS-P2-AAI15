#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "application/application_dispatch.h"

static unsigned publishes;
static bool publish_ok;
static char payload[512];

static bool publish(const char *text, size_t len, void *context)
{
    assert(context == &publishes);
    assert(len < sizeof payload && text[len] == 0);
    memcpy(payload, text, len + 1);
    ++publishes;
    return publish_ok;
}

static application_message_t command(const char *text, uint64_t now)
{
    application_message_t message;
    assert(application_command_parse(text, now, &message));
    return message;
}

static application_dispatch_report_t handle(const application_message_t *message, uint64_t now)
{
    application_dispatch_report_t report;
    assert(application_dispatch_handle(message, sizeof *message, now,
                                        publish, &publishes, &report));
    return report;
}

static void test_processing_and_publication(void)
{
    application_dispatch_init();
    publishes = 0;
    publish_ok = true;
    application_message_t event = command("APP EVENT 1 1 entry", 1000000);
    application_dispatch_report_t r = handle(&event, 1000100);
    assert(r.result.code == APP_RESULT_APPLIED && r.occupancy == 1);
    assert(r.publish_status == APPLICATION_PUBLISH_ENQUEUED);
    assert(r.stats.processed == 1 && r.stats.publish_enqueued == 1);
    assert(publishes == 1 && strstr(payload, "\"occupancy\":1"));

    r = handle(&event, 1000200);
    assert(r.result.code == APP_RESULT_DUPLICATE && r.occupancy == 1);
    assert(publishes == 2 && strstr(payload, "\"result\":2"));

    /* Offline/full publisher refusal must not roll back or retry occupancy. */
    publish_ok = false;
    event = command("APP EVENT 2 2 entry", 1100000);
    r = handle(&event, 1100100);
    assert(r.result.code == APP_RESULT_APPLIED && r.occupancy == 2);
    assert(r.publish_status == APPLICATION_PUBLISH_FAILED && r.stats.publish_failed == 1);
    r = handle(&event, 1100200);
    assert(r.result.code == APP_RESULT_DUPLICATE && r.occupancy == 2);
    assert(r.stats.publish_failed == 2);

    event = command("APP EVENT 3 3 exit", 1200000);
    r = handle(&event, 1200100);
    assert(r.occupancy == 1);
    event = command("APP EVENT 4 4 incomplete", 1300000);
    r = handle(&event, 1300100);
    assert(r.result.code == APP_RESULT_NO_CHANGE && r.occupancy == 1);
    event = command("APP EVENT 5 5 false", 1400000);
    r = handle(&event, 1400100);
    assert(r.result.code == APP_RESULT_NO_CHANGE && r.occupancy == 1);

    /* All processed outcomes, including invalid and stale input, are offered. */
    event = command("APP EVENT 6 6 entry", 1500000);
    event.event.valid = 0;
    r = handle(&event, 1500100);
    assert(r.result.code == APP_RESULT_REJECTED && r.occupancy == 1);
    assert(strstr(payload, "\"degraded\":true"));
    event = command("APP EVENT 7 7 entry", 1600000);
    r = handle(&event, 7600001);
    assert(r.result.code == APP_RESULT_REJECTED && r.occupancy == 1);
    assert(publishes == r.stats.processed);

    unsigned before = publishes;
    event = command("APP STATUS", 8000000);
    r = handle(&event, 8000000);
    assert(r.occupancy == 1 && publishes == before);
    assert(r.publish_status == APPLICATION_PUBLISH_NOT_ATTEMPTED);
    event = command("APP RESET", 8000000);
    r = handle(&event, 8000000);
    assert(r.occupancy == 0 && r.stats.resets == 1 && publishes == before);
    /* Reset clears old duplicate state only when its FIFO item is consumed. */
    event = command("APP EVENT 1 1 entry", 8000000);
    r = handle(&event, 8000100);
    assert(r.result.code == APP_RESULT_APPLIED && r.occupancy == 1);
}

static void test_bad_messages_and_commands(void)
{
    application_dispatch_init();
    application_dispatch_report_t r;
    application_message_t message = command("APP EVENT 1 1 entry", 1000000);
    unsigned before = publishes;
    assert(!application_dispatch_handle(&message, sizeof(event_record_t), 1000000,
                                         publish, &publishes, &r));
    assert(r.stats.malformed == 1 && r.occupancy == 0 && publishes == before);
    assert(!application_dispatch_handle(NULL, 0, 1000000, publish, &publishes, &r));
    message.kind = 999;
    assert(!application_dispatch_handle(&message, sizeof message, 1000000,
                                         publish, &publishes, &r));
    assert(r.stats.malformed == 3 && r.occupancy == 0 && publishes == before);

    const char *bad[] = {
        "", "A", "APP", "APP EVENT", "APP EVENT 0 1 entry", "APP EVENT 1 0 entry",
        "APP EVENT -1 1 entry", "APP EVENT +1 1 entry", "APP EVENT 4294967296 1 entry",
        "APP EVENT 999999999999999999999999 1 entry", "APP EVENT 1x 1 entry",
        "APP EVENT 1 1 entry trailing", "APP EVENT 1 1 unknown", "APP RESET extra",
        "APP STATUS extra", "APP RESETMORE", "APP EVENT 1 1"
    };
    application_message_t original = message;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        assert(!application_command_parse(bad[i], 1000000, &message));
        assert(memcmp(&message, &original, sizeof message) == 0);
    }
    assert(!application_command_parse(NULL, 1, &message));
    assert(!application_command_parse("APP EVENT 1 1 entry", 0, &message));
    message = command(" app event 4294967295 4294967295 EXIT  ", 1000000);
    assert(message.event.event_id == UINT32_MAX && message.event.source_seq == UINT32_MAX);
    assert(message.event.rule_class == EVENT_CLASS_EXIT);
}

static void test_ordered_reset(void)
{
    /* Dispatch cannot apply a queued reset before the consumer reaches it.
     * Kernel FIFO/drop behavior and core residency remain target-board gates. */
    application_message_t fifo[8];
    size_t count = 0;
    application_dispatch_init();
    for (unsigned i = 0; i < 8; ++i) {
        char line[64];
        snprintf(line, sizeof line, "APP EVENT %u %u entry", i + 1, i + 1);
        fifo[count++] = command(line, 1000000 + i);
    }
    assert(count == 8);
    application_message_t newest = command("APP RESET", 1000100);
    application_dispatch_report_t r;
    for (size_t i = 0; i < count; ++i) r = handle(&fifo[i], 1000200);
    assert(r.occupancy == 8 && r.stats.resets == 0);
    r = handle(&newest, 1000300);
    assert(r.occupancy == 0 && r.stats.resets == 1);
    newest = command("APP EVENT 1 1 entry", 1000400);
    r = handle(&newest, 1000500);
    assert(r.occupancy == 1);
}

int main(void)
{
    test_processing_and_publication();
    test_bad_messages_and_commands();
    test_ordered_reset();
    printf("application dispatch: PASS (typed message=%zu, result publication, FIFO reset, malformed input)\n",
           sizeof(application_message_t));
    return 0;
}
