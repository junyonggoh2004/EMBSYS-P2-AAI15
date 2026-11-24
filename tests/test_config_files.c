/* Every config file in config/ and the multi-bus config lines in README 5.4
 * are sent through the board's own config-topic path: the payload goes to the
 * callback that mqtt_config_parser.c registers for pico/<node>/config, which
 * splits each BEGINCFG..ENDCFG block and parses it with config_parser.c, as on
 * the board. The scheduler and the MQTT publisher are recorded here; the
 * scheduler stub refuses a config for the same two reasons the real
 * scheduler_add_config() checks first (invalid timing, parser error).
 *
 * Covers D1 task 1.3 (configuration over MQTT), D2-1 (the SPI contract), D6
 * tasks 5.3, 5.4 and 5.10, and the refusals behind D-01 and D3-1. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hal/hal.h"
#include "bus/bus_common.h"
#include "application/scheduler.h"
#include "application/timing_contract.h"
#include "mqtt/mqtt_telemetry.h"
#include "rule_engine/rules.h"

/* Registers the config, command and node callbacks with the MQTT client. */
bool control_mqtt_start(const char *broker, uint16_t port, const char *client_id, const char *node_id);

#define MAX_ADDED 16
static app_cfg_t added[MAX_ADDED];
static int n_added;
static char replies[MAX_ADDED][48];
static int n_replies;
static mqtt_cfg_block_cb_t config_topic;

int scheduler_add_config(const app_cfg_t *cfg)
{
    const char *reason = NULL;
    if (!cfg || !timing_config_validate(&cfg->timing, &reason) || cfg->cfg_error[0]) return -5;
    assert(n_added < MAX_ADDED);
    added[n_added] = *cfg;
    return n_added++;
}
bool mqtt_pub_cfg_status(const char *name, bool ok)
{
    assert(n_replies < MAX_ADDED);
    snprintf(replies[n_replies++], sizeof replies[0], "%s:%s", ok ? "OK" : "FAIL", name);
    return true;
}
bool mqtt_init(const char *host, uint16_t port, const char *client_id,
               mqtt_cfg_block_cb_t cfg_cb, mqtt_cmd_cb_t cmd_cb, mqtt_node_msg_cb_t node_cb)
{
    (void)host; (void)port; (void)client_id; (void)cmd_cb; (void)node_cb;
    config_topic = cfg_cb;
    return true;
}

/* The board's pin rule, as in hal_mtk.c: GPIO 0-48 except 19/20 (native USB),
 * 22-25 (absent) and 26-32 (SPI flash and PSRAM). */
bool hal_gpio_valid(int pin)
{
    if (pin < 0 || pin >= 49) return false;
    if (pin >= 22 && pin <= 25) return false;
    if (pin == 19 || pin == 20) return false;
    if (pin >= 26 && pin <= 32) return false;
    return true;
}

/* The rest of the board around the config path; none of it is reached here. */
uint64_t hal_time_us(void) { return 0; }
uint32_t hal_time_ms(void) { return 0; }
int hal_console_getc(uint32_t timeout) { (void)timeout; return HAL_ERR_TIMEOUT; }
void mqtt_set_node_id(const char *node_id) { (void)node_id; }
bool mqtt_pub_rule_status(const char *name, bool ok) { (void)name; (void)ok; return true; }
void rules_begin(void) { }
void rules_feed_line(const char *line) { (void)line; }
bool rules_end(void) { return false; }
void rules_reset(void) { }
void rules_dump(void) { }
void fire_action(const rule_t *rule, double value, uint32_t ts_ms) { (void)rule; (void)value; (void)ts_ms; }
void inference_mgr_set_verbose(bool enable) { (void)enable; }
void inference_mgr_set_score_mode(int mode) { (void)mode; }
void sensor_bridge_reset(void) { }
void scheduler_reset(void) { }
void scheduler_dump(void) { }
void scheduler_window_stop(void) { }
void scheduler_pair_cancel(void) { }
int scheduler_window_report(uint32_t token) { (void)token; return HAL_OK; }
int scheduler_window_command(const char *args) { (void)args; return HAL_OK; }
int scheduler_pair_command(const char *args) { (void)args; return HAL_OK; }
int scheduler_doorway_command(const char *args, bool running) { (void)args; (void)running; return HAL_OK; }
int scheduler_trigger(const char *source, uint64_t t) { (void)source; (void)t; return 0; }

static void reset(void) { n_added = 0; n_replies = 0; memset(added, 0, sizeof added); }

static void send_payload(const char *payload) { reset(); config_topic(payload); }

static void send_file(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "rb");
    assert(f && "run from the repository root");
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    send_payload(buf);
}

static const app_cfg_t *find(const char *name)
{
    for (int i = 0; i < n_added; ++i) if (!strcmp(added[i].name, name)) return &added[i];
    return NULL;
}

static void expect_ok(const char *name)
{
    char want[48];
    snprintf(want, sizeof want, "OK:%s", name);
    for (int i = 0; i < n_replies; ++i) if (!strcmp(replies[i], want)) return;
    fprintf(stderr, "missing reply %s\n", want);
    assert(0);
}

static void expect_all_ok(int count)
{
    assert(n_added == count && n_replies == count);
    for (int i = 0; i < n_replies; ++i) assert(!strncmp(replies[i], "OK:", 3));
}

static void expect_refused(const char *payload, const char *name)
{
    char want[48];
    send_payload(payload);
    snprintf(want, sizeof want, "FAIL:%s", name);
    assert(n_added == 0 && n_replies == 1 && !strcmp(replies[0], want));
}

static int count_blocks(const char *path)
{
    char line[1024];
    int n = 0;
    FILE *f = fopen(path, "rb");
    assert(f);
    while (fgets(line, sizeof line, f)) if (strstr(line, "BEGINCFG")) ++n;
    fclose(f);
    return n;
}

static void board_a(void)
{
    send_file("config/board_a.cfg");
    expect_all_ok(4);
    const app_cfg_t *aht = find("AHT"), *light = find("LIGHT"), *a = find("SONAR_A"), *b = find("SONAR_B");
    assert(aht && aht->proto == proto_i2c && aht->i2c_sensor == I2C_SENSOR_AHT &&
           aht->i2c_sda == 1 && aht->i2c_scl == 2 && aht->i2c_addr == 0x38 && aht->timing.interval_ms == 2000);
    assert(light && light->proto == proto_i2c && light->i2c_sensor == I2C_SENSOR_TSL2561 &&
           light->i2c_addr == 0x29 && light->timing.interval_ms == 2000);
    assert(a && a->proto == proto_gpio && !strcmp(a->gpio_mode, "pulse") && a->gpio_trig == 10 &&
           a->gpio_echo == 11 && a->gpio_pulse_timeout_us == 20000 && a->timing.interval_ms == 500);
    assert(b && b->proto == proto_gpio && b->gpio_trig == 6 && b->gpio_echo == 7 && b->timing.interval_ms == 500);
    puts("board_a.cfg: OK:AHT OK:LIGHT OK:SONAR_A OK:SONAR_B (I2C 1/2, sonars 10/11 and 6/7)");
}

static void board_b(void)
{
    static const struct { const char *name, *source; sensor_profile_t profile; uint32_t interval; } follow[] = {
        {"A_AHT", "AHT", SENSOR_PROFILE_AHT, 2000}, {"A_LIGHT", "LIGHT", SENSOR_PROFILE_TSL2561, 2000},
        {"A_SONAR_A", "SONAR_A", SENSOR_PROFILE_PULSE, 500}, {"A_SONAR_B", "SONAR_B", SENSOR_PROFILE_PULSE, 500},
    };
    send_file("config/board_b_ml.cfg");
    expect_all_ok(4);
    for (size_t i = 0; i < sizeof follow / sizeof follow[0]; ++i) {
        const app_cfg_t *c = find(follow[i].name);
        assert(c && c->proto == proto_mqtt && !strcmp(c->remote_node, "s3-001") &&
               !strcmp(c->remote_source, follow[i].source) && c->remote_profile == follow[i].profile &&
               c->timing.interval_ms == follow[i].interval);
    }
    puts("board_b_ml.cfg: B follows s3-001 AHT, LIGHT, SONAR_A and SONAR_B with their decoders");
}

static void people_counter_profiles(void)
{
    send_file("config/board_a_people_counter.cfg");
    expect_all_ok(1);
    const app_cfg_t *a = find("SONAR_A");
    assert(a && a->gpio_trig == 10 && a->gpio_echo == 11 && a->timing.interval_ms == 70 && a->timing.timeout_ms == 30);
    send_file("config/board_b_people_counter.cfg");
    expect_all_ok(1);
    const app_cfg_t *b = find("A_SONAR_A");
    assert(b && b->proto == proto_mqtt && b->remote_profile == SENSOR_PROFILE_PULSE && b->timing.interval_ms == 70);
    puts("people-counter profiles: OK:SONAR_A on A every 70 ms, OK:A_SONAR_A on B every 70 ms");
}

static void other_presets(void)
{
    static const char *files[] = {
        "config/three_sensors.cfg", "config/d3_sonar_v2_once.cfg", "config/demos/dual-ultrasonic.cfg",
        "config/demos/no-aht.cfg", "config/demos/no-light.cfg",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        int blocks = count_blocks(files[i]);
        send_file(files[i]);
        expect_all_ok(blocks);
        printf("%s: %d of %d configs OK\n", files[i], n_added, blocks);
    }
}

/* README 5.4.4 and 5.4.5: one board, four sensors on three buses, timing changed live. */
static void multibus(void)
{
    send_payload("BEGINCFG|name=MH|proto=gpio|gpio.mode=digital|gpio.pin=40|gpio.pull=up|mode=poll|freq_hz=2|ENDCFG");
    expect_all_ok(1); expect_ok("MH");
    assert(added[0].proto == proto_gpio && !strcmp(added[0].gpio_mode, "digital") && added[0].gpio_pin == 40 &&
           !strcmp(added[0].gpio_pull, "up") && added[0].timing.interval_ms == 500);

    send_payload("BEGINCFG|name=ACC|proto=i2c|mode=poll|freq_hz=2|i2c.sda=1|i2c.scl=2|i2c.addr=0x19|i2c.pre=0x20 0x57|"
                 "i2c.reg=0xA8|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG");
    expect_all_ok(1); expect_ok("ACC");
    assert(added[0].proto == proto_i2c && added[0].i2c_addr == 0x19 && added[0].i2c_pre_len == 2 &&
           added[0].i2c_pre[0] == 0x20 && added[0].i2c_pre[1] == 0x57 && added[0].i2c_reg == 0xA8 &&
           added[0].i2c_reg_size == 1 && added[0].i2c_read_len == 6 && added[0].i2c_restart == 1);

    send_payload("BEGINCFG|name=SONAR|proto=gpio|gpio.mode=pulse|gpio.trig=42|gpio.echo=41|gpio.pull=down|gpio.trig_us=10|"
                 "gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG");
    expect_all_ok(1); expect_ok("SONAR");
    assert(added[0].gpio_trig == 42 && added[0].gpio_echo == 41 && added[0].gpio_trig_us == 10 &&
           added[0].timing.interval_ms == 500 && added[0].timing.timeout_ms == 100);

    send_payload("BEGINCFG|name=FLASH_ID|proto=spi|mode=poll|freq_hz=1|spi.host=fspi|spi.sclk=11|spi.mosi=12|spi.miso=13|"
                 "spi.cs=10|spi.hz=1000000|spi.mode=0|spi.tx=0x9F|spi.read_len=4|spi.timeout_ms=100|ENDCFG");
    expect_all_ok(1); expect_ok("FLASH_ID");
    assert(added[0].proto == proto_spi && !strcmp(added[0].spi_host, "fspi") && added[0].spi_sclk == 11 &&
           added[0].spi_mosi == 12 && added[0].spi_miso == 13 && added[0].spi_cs == 10 && added[0].spi_hz == 1000000 &&
           added[0].spi_mode == 0 && added[0].spi_tx_len == 1 && added[0].spi_tx[0] == 0x9F && added[0].spi_read_len == 4);

    /* The same sonar on each timing mode (5.4.5). */
    send_payload("BEGINCFG|name=SONAR|proto=gpio|gpio.mode=pulse|gpio.trig=42|gpio.echo=41|gpio.pull=down|gpio.trig_us=10|"
                 "gpio.pulse_timeout_us=20000|mode=poll|timing.mode=periodic|timing.interval_ms=500|timing.timeout_ms=100|ENDCFG");
    expect_all_ok(1);
    assert(added[0].timing.mode == TIMING_MODE_PERIODIC && added[0].timing.interval_ms == 500);
    send_payload("BEGINCFG|name=SONAR|proto=gpio|gpio.mode=pulse|gpio.trig=42|gpio.echo=41|gpio.pull=down|gpio.trig_us=10|"
                 "gpio.pulse_timeout_us=20000|mode=poll|timing.mode=one_shot|timing.timeout_ms=100|ENDCFG");
    expect_all_ok(1);
    assert(added[0].timing.mode == TIMING_MODE_ONE_SHOT);
    send_payload("BEGINCFG|name=ACC|proto=i2c|mode=poll|i2c.sda=1|i2c.scl=2|i2c.addr=0x19|i2c.pre=0x20 0x57|i2c.reg=0xA8|"
                 "i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|timing.mode=event_driven|timing.trigger_source=SONAR|"
                 "timing.window_ms=50|ENDCFG");
    expect_all_ok(1);
    assert(added[0].timing.mode == TIMING_MODE_EVENT_DRIVEN && !strcmp(added[0].timing_trigger_source, "SONAR") &&
           added[0].timing.window_ms == 50);
    puts("README 5.4: OK:MH (GPIO) OK:ACC (I2C) OK:SONAR (GPIO pulse) OK:FLASH_ID (SPI); periodic, one-shot and event-driven timing");
}

/* The same path refuses bad settings instead of guessing (D-01, D3-1). */
static void refusals(void)
{
    expect_refused("BEGINCFG|name=BAD|proto=nope|mode=poll|freq_hz=1|ENDCFG", "BAD");
    expect_refused("BEGINCFG|name=SAME|proto=gpio|gpio.mode=pulse|gpio.trig=42|gpio.echo=42|mode=poll|freq_hz=2|ENDCFG", "SAME");
    expect_refused("BEGINCFG|name=EVT|proto=gpio|gpio.mode=digital|gpio.pin=40|mode=poll|timing.mode=event_driven|"
                   "timing.window_ms=50|ENDCFG", "EVT");
    puts("refusals: FAIL:BAD (unknown proto), FAIL:SAME (trig equals echo), FAIL:EVT (event timing without a source)");
}

/* D2-1: the SPI contract is enforced on the same path. */
static void spi_contract(void)
{
#define SPI_BASE "proto=spi|mode=poll|freq_hz=1|spi.sclk=11|spi.mosi=12|spi.miso=13|spi.tx=0x9F|spi.read_len=4|spi.timeout_ms=100"
    expect_refused("BEGINCFG|name=HOST|" SPI_BASE "|spi.host=hspi|spi.cs=10|spi.hz=1000000|spi.mode=0|ENDCFG", "HOST");
    expect_refused("BEGINCFG|name=SLOW|" SPI_BASE "|spi.host=fspi|spi.cs=10|spi.hz=50000|spi.mode=0|ENDCFG", "SLOW");
    expect_refused("BEGINCFG|name=MODE|" SPI_BASE "|spi.host=fspi|spi.cs=10|spi.hz=1000000|spi.mode=4|ENDCFG", "MODE");
    expect_refused("BEGINCFG|name=PIN|" SPI_BASE "|spi.host=fspi|spi.cs=27|spi.hz=1000000|spi.mode=0|ENDCFG", "PIN");
    expect_refused("BEGINCFG|name=DUP|" SPI_BASE "|spi.host=fspi|spi.cs=11|spi.hz=1000000|spi.mode=0|ENDCFG", "DUP");
    expect_refused("BEGINCFG|name=BIG|proto=spi|mode=poll|freq_hz=1|spi.host=fspi|spi.sclk=11|spi.mosi=12|spi.miso=13|"
                   "spi.cs=10|spi.hz=1000000|spi.mode=0|spi.tx=0x9F|spi.read_len=129|ENDCFG", "BIG");
    expect_refused("BEGINCFG|name=STREAM|proto=spi|mode=stream|freq_hz=1|spi.host=fspi|spi.sclk=11|spi.mosi=12|spi.miso=13|"
                   "spi.cs=10|spi.hz=1000000|spi.mode=0|spi.tx=0x9F|spi.read_len=4|ENDCFG", "STREAM");
    char line[512] = "BEGINCFG|name=LONG|proto=spi|mode=poll|freq_hz=1|spi.host=fspi|spi.sclk=11|spi.mosi=12|spi.miso=13|"
                     "spi.cs=10|spi.hz=1000000|spi.mode=0|spi.tx=";
    /* Written compactly so the whole field fits the config splitter's 255-character token. */
    for (int i = 0; i < 65; ++i) strcat(line, i ? " 0" : "0");
    strcat(line, "|ENDCFG");
    expect_refused(line, "LONG");
#undef SPI_BASE
    puts("SPI contract: refused hspi, 50 kHz, mode 4, reserved pin 27, shared pins, 129-byte read, stream mode, 65-byte write");
}

int main(void)
{
    assert(control_mqtt_start("broker", 1883, "s3-005", "s3-005") && config_topic);
    board_a();
    board_b();
    people_counter_profiles();
    other_presets();
    multibus();
    refusals();
    spi_contract();
    puts("Config files and README 5.4 config lines: PASS");
    return 0;
}
