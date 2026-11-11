#include <assert.h>
#include <setjmp.h>

/* Compile the real runtime and call its real application/control task bodies.
 * SDK and kernel calls are modeled below; this is not a scheduler emulator. */
#include "../esp32s3_mtk/components/edge_mtk/runtime_mtk.c"

/* Command/task integration uses a recorded LCD boundary; the component's
 * separate HAL test exercises its real drawing and error behavior. */
static unsigned lcd_inits, lcd_patterns, lcd_closes;
static int lcd_init_result = HAL_OK;
static lcd_output_config_t last_lcd;
int lcd_output_init(const lcd_output_config_t *config)
{ ++lcd_inits; last_lcd = *config; return lcd_init_result; }
void lcd_output_deinit(void) { ++lcd_closes; }
int lcd_output_test_pattern(void) { ++lcd_patterns; return HAL_OK; }

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
static queue_t queues[8];
static T_CTSK tasks[16];
static unsigned queue_count, task_count, started_count, fail_create;
static UINT network_flags;
static jmp_buf waiting;
static enum { NO_TASK, APP_TASK, CONTROL_TASK, BENCH_TASK } active_task;
static const char *console_input;

static size_t cost(INT bytes) { return 4u + (((size_t)bytes + 3u) & ~3u); }
static queue_t *queue(ID id) { assert(id > 0 && (unsigned)id <= queue_count); return &queues[id]; }

ID tk_cre_mbf(const T_CMBF *config)
{
    ++queue_count;
    if (queue_count == fail_create) return E_NOMEM;
    assert(queue_count < 8 && config->bufptr && config->maxmsz > 0);
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
        if (timeout == TMO_POL) return E_TMOUT; /* ControlTask polls ctrl_mbf */
        assert(((active_task == APP_TASK && id == app_mbf) ||
                (active_task == BENCH_TASK && id == bench_mbf)) && timeout == TMO_FEVR);
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
static unsigned ml_commands, ml_invalidations, ordinary_commands;
static bool ml_sampling;
bool sensor_ml_command(const char *line,bool sampling)
{
    if (strncmp(line, "ML ", 3)) return false;
    ++ml_commands;
    ml_sampling = sampling;
    return true;
}
void sensor_ml_invalidate(void) { ++ml_invalidations; }
/* D1 task 1.4: the shared REPL command table and the console STOP/RUN side effects. */
static unsigned repl_commands, window_stops, pair_cancels;
static char last_repl[64];
bool repl_command(const char *line, bool *running, bool *ready)
{
    (void)running; (void)ready;
    if (strncasecmp(line, "WINDOW", 6) && strncasecmp(line, "PAIR", 4) && strcasecmp(line, "SHOWRULES"))
        return false;
    ++repl_commands;
    snprintf(last_repl, sizeof last_repl, "%s", line);
    return true;
}
void scheduler_window_stop(void) { ++window_stops; }
void scheduler_pair_cancel(void) { ++pair_cancels; }
void sensor_ml_poll(bool sampling) { (void)sampling; }
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
static unsigned remote_frames;
void sensor_bridge_on_remote_line(const char *node, const char *source, const char *line, uint32_t ts)
{
    assert(!strcmp(node, "nodeB") && !strcmp(source, "AHT"));
    assert(!strcmp(line, "proto=i2c src=AHT len=1 ts=0 : 00") && ts == 1000);
    ++remote_frames;
}
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
static void consume_bench(void)
{
    active_task = BENCH_TASK;
    if (!setjmp(waiting)) benchmark_task(0, NULL);
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

static char last_mqtt[64];
static void test_mqtt_command(const char *line)
{
    assert(!strcmp(line, "SHOW") || !strcmp(line, "STOP") || !strcmp(line, "RUN") || !strcmp(line, "FOO"));
    ++ordinary_commands;
    snprintf(last_mqtt, sizeof last_mqtt, "%s", line);
}
static void mqtt_cmd(const char *text, bool *running, bool *ready)
{
    static sched_msg_t m = {.kind = SCHED_MQTT_CMD};
    snprintf(m.text, sizeof m.text, "%s", text);
    handle_sched_msg(&m, running, ready);
}

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
    assert(sizeof(bench_msg_t) == 40);
    assert(queue(bench_mbf)->config.maxmsz == sizeof(bench_msg_t));
    assert(queue(bench_mbf)->config.bufsz == BENCH_MBF_MSGS * cost(sizeof(bench_msg_t)));
    printf("Native bench_msg_t=%zu, bench_mbf=%zu bytes (target layout still needs image audit)\n",
           sizeof(bench_msg_t), (size_t)queue(bench_mbf)->config.bufsz);

    /* MQTT ML commands use the same SchedulerTask handler as serial ML commands. */
    runtime_set_mqtt_handlers(NULL, test_mqtt_command, NULL);
    bool running = true, cfg_ready = true;
    /* Topic parsing cannot silently truncate an identity, and embedded NULs
     * cannot hide trailing bytes before the bridge validates the envelope. */
    sched_msg_t remote = {.kind = SCHED_REMOTE_LINE};
    strcpy(remote.topic, "pico/nodeB/sensor/AHT/line");
    strcpy(remote.text, "proto=i2c src=AHT len=1 ts=0 : 00");
    remote.len = (uint16_t)strlen(remote.text);
    handle_sched_msg(&remote, &running, &cfg_ready);
    assert(remote_frames == 1);
    remote.text[remote.len + 1] = 'X';
    remote.len += 2;
    handle_sched_msg(&remote, &running, &cfg_ready);
    assert(remote_frames == 1);
    remote.len -= 2;
    strcpy(remote.topic, "pico/12345678901234567890123456789012/sensor/AHT/line");
    handle_sched_msg(&remote, &running, &cfg_ready);
    assert(remote_frames == 1);
    memset(remote.topic, 'X', sizeof remote.topic); /* no terminator */
    handle_sched_msg(&remote, &running, &cfg_ready);
    assert(remote_frames == 1);
    remote.len = sizeof remote.text;
    handle_sched_msg(&remote, &running, &cfg_ready);
    assert(remote_frames == 1);
    sched_msg_t ml = {.kind = SCHED_MQTT_CMD};
    strcpy(ml.text, "ML SHOW");
    handle_sched_msg(&ml, &running, &cfg_ready);
    assert(ml_commands == 1 && ml_sampling && ml_invalidations == 0 && ordinary_commands == 0);
    strcpy(ml.text, "SHOW");
    handle_sched_msg(&ml, &running, &cfg_ready);
    assert(ordinary_commands == 1 && ml_invalidations == 0);
    strcpy(ml.text, "STOP");
    handle_sched_msg(&ml, &running, &cfg_ready);
    assert(ordinary_commands == 2 && ml_invalidations == 1);

    /* D1 task 1.4: every console command also works on pico/<node>/cmd. */
    assert(queue(ctrl_mbf)->config.maxmsz == sizeof(ctrl_msg_t));
    assert(queue(ctrl_mbf)->config.bufsz == CTRL_MBF_MSGS * cost(sizeof(ctrl_msg_t)));
    /* STOP and RUN keep the MQTT reply and gain the console's WINDOW/PAIR effects. */
    assert(window_stops == 1 && pair_cancels == 1);
    mqtt_cmd(" RUN\r\n", &running, &cfg_ready);
    assert(ordinary_commands == 3 && !strcmp(last_mqtt, "RUN") && pair_cancels == 2 && window_stops == 1);
    /* Shared REPL commands run in SchedulerTask through the same table as the console. */
    mqtt_cmd("WINDOW STATUS 0000abcd", &running, &cfg_ready);
    assert(repl_commands == 1 && !strcmp(last_repl, "WINDOW STATUS 0000abcd") && ordinary_commands == 3);
    mqtt_cmd("SHOWRULES", &running, &cfg_ready);
    assert(repl_commands == 2 && ordinary_commands == 3);
    /* Unknown text still gets the MQTT handler's "[CMD] Unknown" reply. */
    mqtt_cmd("FOO", &running, &cfg_ready);
    assert(repl_commands == 2 && ordinary_commands == 4 && !strcmp(last_mqtt, "FOO"));
    /* Configuration is refused on the command topic; it has its own topic. */
    mqtt_cmd("BEGINCFG|name=X|proto=gpio|ENDCFG", &running, &cfg_ready);
    assert(repl_commands == 2 && ordinary_commands == 4 && queue(ctrl_mbf)->count == 0);
    /* Platform commands go to ControlTask, which runs them as typed lines. */
    mqtt_cmd("APP STATUS", &running, &cfg_ready);
    mqtt_cmd("app reset", &running, &cfg_ready);
    assert(queue(ctrl_mbf)->count == 2 && queue(app_mbf)->count == 0 && ordinary_commands == 4);
    control("");
    assert(queue(ctrl_mbf)->count == 0 && queue(app_mbf)->count == 2);
    consume_app();
    assert(queue(app_mbf)->count == 0);
    clear_queue(pub_mbf);
    /* A bad platform command reaches ControlTask and is rejected there. */
    mqtt_cmd("SPICHECK", &running, &cfg_ready);
    control("");
    assert(queue(ctrl_mbf)->count == 0 && queue(bench_mbf)->count == 0);
    /* A full ctrl_mbf drops the newest command and counts it. */
    uint32_t ctrl_drops = dropped(ctrl_mbf);
    for (unsigned i = 0; i < CTRL_MBF_MSGS + 1; ++i) mqtt_cmd("APP STATUS", &running, &cfg_ready);
    assert(queue(ctrl_mbf)->count == CTRL_MBF_MSGS && dropped(ctrl_mbf) == ctrl_drops + 1);
    control("");
    consume_app();
    clear_queue(pub_mbf);
    assert(queue(ctrl_mbf)->count == 0 && queue(app_mbf)->count == 0);

    /* LCD diagnostic has the same path from MQTT and serial. It remains
     * inactive at startup and only BenchmarkTask calls the driver. */
    assert(lcd_inits == 0 && lcd_patterns == 0 && lcd_closes == 0);
    control("LCDTEST 76 284 82 18 00 0\n");
    assert(queue(bench_mbf)->count == 1 && lcd_inits == 0);
    bench_msg_t lcd_request;
    assert(tk_rcv_mbf(bench_mbf, &lcd_request, TMO_POL) == sizeof lcd_request);
    assert(!strcmp(lcd_request.name, "LCDTEST"));
    assert(lcd_request.lcd_width == 76 && lcd_request.lcd_height == 284);
    assert(lcd_request.lcd_x_offset == 82 && lcd_request.lcd_y_offset == 18);
    assert(lcd_request.lcd_madctl == 0 && lcd_request.lcd_invert == 0);
    const char *bad_lcd[] = {
        "LCDTEST", "LCDTEST 0 240 0 0 00 1", "LCDTEST 76 284 82 40 00 0",
        "LCDTEST 76 284 82 18 100 0", "LCDTEST 76 284 82 18 00 2",
        "LCDTEST 76 284 82 18 00 0 junk", "LCDTEST -1 284 82 18 00 0",
        "LCDTEST 4294967372 284 82 18 00 0", "LCDTEST +76 284 82 18 00 0",
        "LCDTEST 76 284 82 18 00 -4294967296", "LCDTEST 76 284 82 18 0x 0",
    };
    for (size_t i = 0; i < sizeof bad_lcd / sizeof bad_lcd[0]; ++i) {
        assert(platform_command(bad_lcd[i]));
        assert(queue(bench_mbf)->count == 0);
    }
    assert(!platform_command("LCDTESTX 76 284 82 18 00 0"));
    mqtt_cmd("lcdtest 76 284 82 18 00 0", &running, &cfg_ready);
    assert(queue(ctrl_mbf)->count == 1 && queue(bench_mbf)->count == 0);
    control("");
    assert(queue(ctrl_mbf)->count == 0 && queue(bench_mbf)->count == 1);
    consume_bench();
    assert(lcd_inits == 1 && lcd_patterns == 1 && lcd_closes == 2);
    assert(last_lcd.width == 76 && last_lcd.height == 284 && !last_lcd.invert_colors);
    lcd_init_result = HAL_ERR_IO;
    control("LCDTEST 76 284 82 18 00 0\n");
    consume_bench();
    assert(lcd_inits == 2 && lcd_patterns == 1 && lcd_closes == 4);
    lcd_init_result = HAL_OK;
    uint32_t lcd_drops = dropped(bench_mbf);
    for (unsigned i = 0; i < BENCH_MBF_MSGS + 1; ++i)
        assert(platform_command("LCDTEST 76 284 82 18 00 0"));
    assert(queue(bench_mbf)->count == BENCH_MBF_MSGS && dropped(bench_mbf) == lcd_drops + 1);
    clear_queue(bench_mbf);
    runtime_bench("TEST", 123);
    assert(tk_rcv_mbf(bench_mbf, &lcd_request, TMO_POL) == sizeof lcd_request);
    assert(!strcmp(lcd_request.name, "TEST") && lcd_request.value_us == 123);
    assert(lcd_request.lcd_width == 0 && lcd_request.lcd_height == 0);

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
