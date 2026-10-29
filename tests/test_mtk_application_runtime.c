#include <assert.h>
#include <setjmp.h>

/* Compile the real runtime and call its real application/control task bodies.
 * SDK and kernel calls are modeled below; this is not a scheduler emulator. */
#include "../esp32s3_mtk/components/edge_mtk/runtime_mtk.c"

typedef struct message {
    struct message *next;
    INT bytes;
    unsigned char data[];
} message_t;
typedef struct {
    T_CMBF config;
    size_t used;
    unsigned count;
    message_t *head, *tail;
} queue_t;
static queue_t queues[6];
static T_CTSK tasks[16];
static unsigned queue_count, task_count, started_count, fail_create;
static UINT network_flags;
static jmp_buf waiting;
static enum { NO_TASK, APP_TASK, CONTROL_TASK } active_task;
static const char *console_input;

static size_t cost(INT bytes) { return 4u + (((size_t)bytes + 3u) & ~3u); }
static queue_t *queue(ID id) { assert(id > 0 && (unsigned)id <= queue_count); return &queues[id]; }

ID tk_cre_mbf(const T_CMBF *config)
{
    ++queue_count;
    if (queue_count == fail_create) return E_NOMEM;
    assert(queue_count < 6 && config->bufptr && config->maxmsz > 0);
    assert((config->mbfatr & (TA_USERBUF | TA_TFIFO)) == (TA_USERBUF | TA_TFIFO));
    queues[queue_count].config = *config;
    return (ID)queue_count;
}
ER tk_snd_mbf(ID id, void *data, INT bytes, TMO timeout)
{
    assert(timeout == TMO_POL); /* Every producer path exercised is zero-wait. */
    queue_t *q = queue(id);
    assert(data && bytes > 0 && bytes <= q->config.maxmsz);
    if (q->used + cost(bytes) > q->config.bufsz) return E_TMOUT;
    message_t *m = malloc(sizeof *m + (size_t)bytes);
    assert(m);
    m->next = NULL;
    m->bytes = bytes;
    memcpy(m->data, data, (size_t)bytes);
    if (q->tail) q->tail->next = m;
    else q->head = m;
    q->tail = m;
    q->used += cost(bytes);
    ++q->count;
    return E_OK;
}
INT tk_rcv_mbf(ID id, void *out, TMO timeout)
{
    queue_t *q = queue(id);
    if (!q->head) {
        assert(active_task == APP_TASK && id == app_mbf && timeout == TMO_FEVR);
        longjmp(waiting, 1); /* stop where the real task would block */
    }
    message_t *m = q->head;
    INT bytes = m->bytes;
    memcpy(out, m->data, (size_t)bytes);
    q->head = m->next;
    if (!q->head) q->tail = NULL;
    q->used -= cost(bytes);
    --q->count;
    free(m);
    return bytes;
}
ER tk_ref_mbf(ID id, T_RMBF *out)
{ queue_t *q = queue(id); out->frbufsz = q->config.bufsz - q->used; return E_OK; }
ID tk_cre_flg(const T_CFLG *config) { network_flags = config->iflgptn; return 6; }
ER tk_ref_flg(ID id, T_RFLG *out) { assert(id == net_flg); out->flgptn = network_flags; return E_OK; }
ID tk_cre_tsk(const T_CTSK *config)
{ assert(task_count < 16); tasks[task_count++] = *config; return (ID)(19 + task_count); }
ER tk_sta_tsk(ID id, INT start)
{ assert(id >= 20 && id < 20 + (ID)task_count && start == 0); ++started_count; return E_OK; }
ER tk_ref_tsk(ID id, T_RTSK *out) { out->tskpri = tasks[id - 20].itskpri; return E_OK; }
ID tk_cre_cyc(const T_CCYC *config) { assert(config->cyctim == 1000); return 40; }
ID tk_get_prc(void) { return 2; } /* mock core 1; not measured residency */
ER tk_dly_tsk(TMO delay) { (void)delay; abort(); }
ER tk_slp_tsk(TMO timeout) { (void)timeout; abort(); }
ER tk_wup_tsk(ID id) { (void)id; abort(); }

uint64_t hal_time_us(void) { return 1000000; }
uint32_t hal_time_ms(void) { return 1000; }
int hal_console_getc(uint32_t timeout)
{
    assert(timeout == 100000 && active_task == CONTROL_TASK);
    if (!*console_input) longjmp(waiting, 1);
    return (unsigned char)*console_input++;
}
bool repl_line_feed(int ch, char *line, size_t capacity, size_t *len)
{
    /* The REPL byte-assembly boundary is unrelated to these task tests. */
    assert(*len + 1 < capacity);
    if (ch == '\n') { line[*len] = 0; return true; }
    line[(*len)++] = (char)ch;
    return false;
}
ER console_start_rx(void) { return E_OK; }
ER inference_mtk_init(void) { return E_OK; }

/* Unrelated task/hardware services must not execute in this fixture. */
#define UNUSED_VOID(name) void name(void) { abort(); }
UNUSED_VOID(mqtt_task_body)
UNUSED_VOID(inference_task_body)
UNUSED_VOID(mqtt_wifi_scan)
UNUSED_VOID(mqtt_wifi_drop)
UNUSED_VOID(mtk_spi_flash_id)
UNUSED_VOID(mtk_spi_flash_log_plan)
UNUSED_VOID(mtk_spi_flash_log_write)
UNUSED_VOID(mtk_spi_flash_log_verify)
UNUSED_VOID(model_transfer_init)
UNUSED_VOID(model_transfer_poll)
UNUSED_VOID(inference_mgr_init)
UNUSED_VOID(inference_mgr_poll)
UNUSED_VOID(scheduler_reset)
#define UNUSED_ID(name) void name(uint32_t id) { (void)id; abort(); }
UNUSED_ID(mtk_spi_flash_check)
UNUSED_ID(mtk_spi_flash_scan)
UNUSED_ID(mtk_spi_flash_program)
UNUSED_ID(mtk_spi_flash_verify)
void mtk_spi_benchmark(unsigned mode, uint32_t hz) { (void)mode; (void)hz; abort(); }
void control_bind_flags(bool *running, bool *ready) { (void)running; (void)ready; abort(); }
bool control_mqtt_start(const char *host, uint16_t port, const char *client, const char *node)
{ (void)host; (void)port; (void)client; (void)node; abort(); }
void repl_handle_line(char *line, bool *running, bool *ready)
{ (void)line; (void)running; (void)ready; abort(); }
void repl_init_defaults(app_cfg_t *config) { (void)config; abort(); }
void scheduler_run_all(bool running) { (void)running; abort(); }
uint32_t scheduler_next_due_ms(bool running, uint32_t max) { (void)running; (void)max; abort(); }
bool inference_mgr_load_base_model(void) { abort(); }
void inference_mgr_set_params(const uint8_t *data, size_t bytes) { (void)data; (void)bytes; abort(); }
void sensor_bridge_on_remote_line(const char *node, const char *source, const char *line, uint32_t ts)
{ (void)node; (void)source; (void)line; (void)ts; abort(); }
ER hal_mtk_init(void) { abort(); }
void console_stats(uint32_t *irqs, uint32_t *drops) { *irqs = 0; *drops = 0; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 100000; }
size_t heap_caps_get_minimum_free_size(unsigned caps) { (void)caps; return 100000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 100000; }
void timg_ll_enable_bus_clock(int group, bool enabled) { (void)group; (void)enabled; abort(); }
void timg_ll_reset_register(int group) { (void)group; abort(); }
void wdt_hal_init(wdt_hal_context_t *c, int timer, int prescaler, bool enabled)
{ (void)c; (void)timer; (void)prescaler; (void)enabled; abort(); }
#define UNUSED_WDT(name) void name(wdt_hal_context_t *context) { (void)context; abort(); }
UNUSED_WDT(wdt_hal_write_protect_disable)
UNUSED_WDT(wdt_hal_write_protect_enable)
UNUSED_WDT(wdt_hal_enable)
UNUSED_WDT(wdt_hal_feed)
void wdt_hal_config_stage(wdt_hal_context_t *c, int stage, int ticks, int action)
{ (void)c; (void)stage; (void)ticks; (void)action; abort(); }

static void consume_app(void)
{
    active_task = APP_TASK;
    if (!setjmp(waiting)) application_task(0, NULL);
    active_task = NO_TASK;
}
static void control(const char *text)
{
    console_input = text;
    active_task = CONTROL_TASK;
    if (!setjmp(waiting)) control_task(0, NULL);
    active_task = NO_TASK;
}
static application_dispatch_report_t status(void)
{
    application_message_t message = {.kind = APPLICATION_MESSAGE_STATUS};
    application_dispatch_report_t report;
    assert(application_dispatch_handle(&message, sizeof message, hal_time_us(), NULL, NULL, &report));
    return report;
}
static uint32_t dropped(ID id)
{
    for (size_t i = 0; i < sizeof s_mbfs / sizeof s_mbfs[0]; ++i)
        if (*s_mbfs[i].id == id) return s_mbfs[i].dropped;
    abort();
}
static event_record_t event(unsigned id, unsigned seq)
{
    application_message_t message;
    char text[80];
    snprintf(text, sizeof text, "APP EVENT %u %u entry", id, seq);
    assert(application_command_parse(text, hal_time_us(), &message));
    message.event.source_id = 2;
    return message.event;
}
static void clear_queue(ID id)
{
    queue_t *q = queue(id);
    while (q->head) {
        message_t *m = q->head;
        q->head = m->next;
        free(m);
    }
    q->tail = NULL;
    q->count = 0;
    q->used = 0;
}
static void reset_application(void)
{ control("APP RESET\n"); consume_app(); clear_queue(pub_mbf); clear_queue(bench_mbf); }

int main(void)
{
    puts("NATIVE TEST: modeled kernel/SDK boundaries; no target ABI or scheduling evidence");
    fail_create = 4;
    assert(runtime_start() == E_NOMEM && task_count == 0 && started_count == 0);
    memset(queues, 0, sizeof queues); /* simulate a fresh boot after failed creation */
    queue_count = 0;
    fail_create = 0;
    assert(runtime_start() == E_OK && task_count == 9 && started_count == 9);
    const T_CTSK *app = &tasks[s_tasks[RT_APPLICATION].id - 20];
    assert(app->assprc == TP_PRC2 && app->itskpri == 23 && app->stksz == 4096);
    assert((app->tskatr & (TA_ASSPRC | TA_FPU)) == (TA_ASSPRC | TA_FPU));
    assert(queue(app_mbf)->config.maxmsz == sizeof(application_message_t));
    assert(queue(app_mbf)->config.bufsz == 8 * cost(sizeof(application_message_t)));

    /* Real ControlTask queues reset, event and status in order. */
    control("APP RESET\nAPP EVENT 91 1 entry\nAPP STATUS\n");
    assert(queue(app_mbf)->count == 3 && status().occupancy == 0);
    consume_app();
    assert(status().occupancy == 1 && queue(app_mbf)->count == 0);
    reset_application();

    network_flags = NET_MQTT_UP;
    uint32_t drops = dropped(app_mbf);
    for (unsigned i = 0; i < 8; ++i) {
        event_record_t e = event(200 + i, i + 1);
        assert(event_record_submit(&e)); /* startup sink is the real runtime adapter */
        e.event_id = 9999; /* producer memory is not retained by the FIFO */
    }
    event_record_t extra = event(208, 9);
    assert(!runtime_submit_event(&extra) && dropped(app_mbf) == drops + 1);
    uint32_t resets = status().stats.resets;
    control("APP RESET\n"); /* a full FIFO rejects reset rather than mutating state */
    assert(dropped(app_mbf) == drops + 2 && queue(app_mbf)->count == 8);
    assert(!runtime_submit_event(NULL) && status().occupancy == 0);
    consume_app();
    assert(status().occupancy == 8 && status().stats.resets == resets && queue(pub_mbf)->count == 8);
    for (unsigned i = 0; i < 8; ++i) {
        pub_msg_t out;
        assert(tk_rcv_mbf(pub_mbf, &out, TMO_POL) == sizeof out);
        char expected[40];
        snprintf(expected, sizeof expected, "\"event_id\":%u,", 200 + i);
        assert(strstr(out.payload, expected));
        assert(!strcmp(out.topic, "pico/s3-001/application/event"));
        assert(out.op == PUB_PUBLISH && out.qos == 1 && !out.retain);
        assert(out.len == strlen(out.payload));
    }
    assert(queue(bench_mbf)->count == 8);

    reset_application();
    pub_msg_t filler = {0};
    for (unsigned i = 0; i < PUB_MBF_MSGS; ++i)
        assert(runtime_send(pub_mbf, &filler, sizeof filler));
    drops = dropped(pub_mbf);
    uint32_t failed = status().stats.publish_failed;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < 8; ++i) {
            event_record_t e = event(300 + i, i + 1);
            assert(runtime_submit_event(&e));
        }
        consume_app();
        assert(status().occupancy == 8); /* retrying duplicates cannot reapply */
    }
    assert(dropped(pub_mbf) == drops + 16);
    assert(status().stats.publish_failed == failed + 16);
    uint32_t bench_drops = dropped(bench_mbf);
    extra = event(300, 1);
    assert(runtime_submit_event(&extra));
    consume_app();
    assert(status().occupancy == 8 && dropped(bench_mbf) == bench_drops + 1);

    reset_application();
    network_flags = 0;
    drops = dropped(pub_mbf);
    failed = status().stats.publish_failed;
    extra = event(400, 1);
    assert(runtime_submit_event(&extra));
    consume_app();
    assert(status().occupancy == 1 && queue(pub_mbf)->count == 0);
    assert(dropped(pub_mbf) == drops && status().stats.publish_failed == failed + 1);
    uint32_t malformed = status().stats.malformed;
    uint32_t short_message = 999;
    assert(runtime_send(app_mbf, &short_message, sizeof short_message));
    consume_app();
    assert(status().occupancy == 1 && status().stats.malformed == malformed + 1);
    for (ID id = 1; id <= (ID)queue_count; ++id) clear_queue(id);
    puts("micro T-Kernel application glue: PASS (modeled kernel, actual runtime task bodies)");
    return 0;
}
