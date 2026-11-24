#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "application/scheduler.h"
#include "application/sensor_measurement.h"
#include "inference_engine/inference_manager.h"
#include "inference_engine/tflite_wrapper.h"
static uint32_t now,rev;
static int runs,loads,dim=1,fail_loads;
static bool ready=true,fail_invoke;
static float seen[31];
static int output_dim=1;
static int published_results, published_status;
static unsigned comfort_updates, last_comfort_score;
static bool status_ok;
static char last_published[512];
bool mqtt_pub_ml_result(const char *json) {
    published_results++;
    snprintf(last_published, sizeof last_published, "%s", json);
    return true;
}
bool mqtt_pub_d6_comfort(unsigned score) {
    comfort_updates++;
    last_comfort_score = score;
    return true;
}
bool mqtt_pub_ml_status(bool ok, const char *reason, unsigned long generation) {
    (void)reason; (void)generation;
    published_status++;
    status_ok = ok;
    return true;
}
const uint8_t _binary_comfort_summer_tflite_start[] = {1};
const uint8_t _binary_comfort_summer_tflite_end[] = {0};
#ifdef HAVE_PEOPLE_COUNTER_MODEL
const uint8_t _binary_people_counter_raw_int8_tflite_start[] = {1};
const uint8_t _binary_people_counter_raw_int8_tflite_end[] = {0};
#endif
const unsigned char g_autoencoder_model_data[]={1};
const int g_autoencoder_model_size=1;
static app_cfg_t configs[4];
uint32_t hal_time_ms(void) { return now; }
uint64_t hal_time_us(void) { return (uint64_t)now*1000; }
const app_cfg_t *scheduler_config_by_name(const char *name) {
    for(int i=0;i<4;i++) if(!strcmp(name,configs[i].name)) return &configs[i];
    return NULL;
}
const app_cfg_t *scheduler_config_following(const char *node, const char *source) {
    for(int i=0;i<4;i++)
        if(configs[i].proto==proto_mqtt && !strcmp(node,configs[i].remote_node) &&
           !strcmp(source,configs[i].remote_source)) return &configs[i];
    return NULL;
}
/* The ML sensor frame handed to this board's rules (output "local"). */
static uint8_t local_frame[32];
static int local_len, local_frames;
void output_emit_hex(const app_cfg_t *cfg, const char *proto, const char *src, const uint8_t *buf, int n) {
    assert(cfg && !strcmp(cfg->name, "ML") && !strcmp(proto, "ml") && !strcmp(src, "ML"));
    assert(n > 0 && n <= (int)sizeof local_frame && n % 2 == 0);
    memcpy(local_frame, buf, (size_t)n);
    local_len = n;
    local_frames++;
}
static int16_t local_s16(int i) { return (int16_t)(local_frame[2*i] | (local_frame[2*i+1] << 8)); }
/* The model last sent over MQTT into flash. */
static const uint8_t uploaded_model[] = {7, 7};
static uint32_t uploaded_size;
static int uploaded_dim = 3, uploaded_out = 3;
const uint8_t *model_transfer_get_model_ptr(void) { return uploaded_size ? uploaded_model : NULL; }
uint32_t model_transfer_model_size(void) { return uploaded_size; }
bool inference_mgr_load_bytes(const uint8_t *bytes,uint32_t size) {
    (void)bytes; loads++; rev++;
    if(fail_loads) { fail_loads--; ready=false; return false; }
    ready = true;
    dim = size == 16440 ? 16 : 1;
    output_dim = dim;
    if (bytes == _binary_comfort_summer_tflite_start) {
        dim = 2;
        output_dim = 100;
    }
#ifdef HAVE_PEOPLE_COUNTER_MODEL
    if (bytes == _binary_people_counter_raw_int8_tflite_start) {
        dim = 31;
        output_dim = 3;
    }
#endif
    if (bytes == uploaded_model) {
        dim = uploaded_dim;
        output_dim = uploaded_out;
    }
    return true;
}
uint32_t inference_mgr_revision(void) { return rev; }
bool inference_mgr_is_ready(void) { return ready; }
bool inference_mgr_set_inputs(const float *p,int n) { assert(n==dim); memcpy(seen,p,(size_t)n*sizeof(float)); return true; }
bool inference_mgr_run(void) { runs++; return !fail_invoke; }
int inference_mgr_input_count(void) { return dim; }
int inference_mgr_output_count(void) { return output_dim; }
bool inference_mgr_get_output(int i,float *v) {
    assert(i < output_dim);
    *v = dim == 2 ? (i == 78 ? 0.8f : 0.0f) :
         dim == 31 ? (i == 2 ? 0.8f : 0.0f) : seen[i];
    return true;
}
static char last_line[1800];
static int capture_puts(const char *line) {
    snprintf(last_line, sizeof last_line, "%s", line);
    return puts(line);
}
/* Include the actual implementation, exposing private state only to assertions. */
#define puts capture_puts
#include "../esp32s3_mtk/components/edge_mtk/sensor_ml.c"
#undef puts
static const uint8_t aht[]={8,0x80,0,6,0,0};
static const uint8_t light[]={0,0,0,0,0,0,0,0};
static const uint8_t pulse[]={0xd0,7,1};
static void samples(void) {
    sensor_measurement_emit(&configs[0],aht,6);
    sensor_measurement_emit(&configs[1],light,8);
    sensor_measurement_emit(&configs[2],pulse,3);
}
static void cmd(const char *s) { assert(sensor_ml_command(s,false)); }
static void tick(void) { now+=interval_ms; sensor_ml_poll(true); }

#ifdef HAVE_PEOPLE_COUNTER_MODEL
static void direction_echo(unsigned width, uint32_t elapsed_ms)
{
    const uint8_t frame[] = {(uint8_t)width, (uint8_t)(width >> 8), 1};
    now += elapsed_ms;
    sensor_measurement_notify(&configs[2], frame, sizeof frame);
    sensor_ml_poll(true);
}

static void test_direction_window(void)
{
    const char *setup = "{\"ml_setup\":{\"version\":1,\"model\":\"people-counter\",\"inputs\":["
        "{\"index\":0,\"source\":{\"kind\":\"local\",\"name\":\"DISTANCE\"},"
        "\"field\":\"distance_cm\",\"min\":0,\"max\":200}],\"output\":{\"kind\":\"publish\"}}}";
    configs[2].mode = sample_mode_poll;
    configs[2].timing.mode = TIMING_MODE_PERIODIC;
    configs[2].timing.enabled = true;
    configs[2].timing.interval_ms = 140;
    uint32_t before = generation;
    assert(sensor_ml_setup_json(setup, true));
    assert(!status_ok && generation == before);

    configs[2].timing.interval_ms = 70;
    cmd("ML BIND 0 DISTANCE distance_cm 0 200");
    cmd("ML RUN");
    assert(enabled);
    now = UINT32_MAX - 700; // the first full window crosses timer wrap
    int before_runs = runs;
    for (unsigned i = 0; i < 30; ++i) direction_echo(2000 + i, 70);
    sensor_measurement_notify(&configs[1], light, sizeof light);
    sensor_ml_poll(true);
    assert(runs == before_runs);
    direction_echo(2030, 70);
    assert(runs == before_runs + 1);
    assert(fabsf(seen[0] - 2000 * 0.00001715f) < 1e-6f);
    assert(fabsf(seen[30] - 2030 * 0.00001715f) < 1e-6f);
    assert(strstr(last_line, "\"input_source\":\"sensors\"") && strstr(last_line, "\"direction\":\"out\""));
    sensor_ml_poll(true);
    assert(runs == before_runs + 1); // the same window cannot predict twice
    direction_echo(12000, 70); // valid echo beyond 200 cm, source's far marker
    assert(runs == before_runs + 2);
    assert(fabsf(seen[0] - 2001 * 0.00001715f) < 1e-6f && seen[30] == -0.001f);

    sensor_measurement_notify(&configs[2], NULL, HAL_ERR_TIMEOUT);
    sensor_ml_poll(true);
    assert(direction_window.count == 0 && runs == before_runs + 2);
    for (unsigned i = 0; i < 30; ++i) direction_echo(2500, 70);
    assert(runs == before_runs + 2);
    direction_echo(2500, 70);
    assert(runs == before_runs + 3);
    direction_echo(2500, 140); // a missed poll starts a new window
    assert(direction_window.count == 1 && runs == before_runs + 3);
    now += 96;
    sensor_ml_poll(true);
    assert(direction_window.count == 0);

    assert(sensor_ml_setup_json(setup, true));
    assert(status_ok && publish_output && direction_window.count == 0);
    int before_publish = published_results;
    for (unsigned i = 0; i < 31; ++i) direction_echo(3000, 70);
    assert(published_results == before_publish + 1 && strstr(last_published, "\"direction\":\"out\""));
    sensor_ml_invalidate(); // scheduler STOP, CLEAR or source reconfiguration
    direction_echo(3000, 70);
    assert(direction_window.count == 1 && published_results == before_publish + 1);

    /* Board B follows A's pulse frames under a typed MQTT alias. One binding
       supplies a 31-sample window; the other models keep their native shapes. */
    app_cfg_t saved = configs[3];
    memset(&configs[3], 0, sizeof configs[3]);
    strcpy(configs[3].name, "A_SONAR_A");
    strcpy(configs[3].remote_node, "board-a");
    strcpy(configs[3].remote_source, "SONAR_A");
    configs[3].proto = proto_mqtt;
    configs[3].remote_profile = SENSOR_PROFILE_PULSE;
    configs[3].mode = sample_mode_poll;
    configs[3].timing.mode = TIMING_MODE_PERIODIC;
    configs[3].timing.enabled = true;
    configs[3].timing.interval_ms = 500;
    const char *remote = "{\"ml_setup\":{\"version\":1,\"model\":\"people-counter\",\"inputs\":["
        "{\"index\":0,\"source\":{\"kind\":\"remote\",\"node\":\"board-a\",\"name\":\"SONAR_A\"},"
        "\"field\":\"distance_cm\",\"min\":0,\"max\":200}],\"output\":{\"kind\":\"publish\"}}}";
    assert(sensor_ml_setup_json(remote, true) && status_ok && enabled);
    assert(sensor_measurement_remote_ml_only("A_SONAR_A"));
    assert(!sensor_measurement_remote_ml_only("AHT"));
    app_cfg_t bridge = configs[3];
    bridge.mode = sample_mode_stream; /* the bridge emits with a temporary config */
    before_runs = runs;
    for (unsigned i = 0; i < 31; ++i) {
        const uint8_t frame[] = {(uint8_t)(3000 + i), (uint8_t)((3000 + i) >> 8), 1};
        now += 500;
        sensor_measurement_notify(&bridge, frame, sizeof frame);
        sensor_ml_poll(true);
    }
    assert(runs == before_runs + 1 && direction_window.count == 31);
    assert(strstr(last_published, "\"input_source\":\"remote:board-a\""));
    assert(fabsf(seen[0] - 3000 * 0.00001715f) < 1e-6f);
    /* The dedicated B people-counter profile follows A's 70 ms single-sonar
       stream. A normal 500 ms bridge must still work independently. */
    configs[3].timing.interval_ms = 70;
    sensor_ml_invalidate();
    before_runs = runs;
    for (unsigned i = 0; i < 31; ++i) {
        const uint8_t frame[] = {(uint8_t)(3200 + i), (uint8_t)((3200 + i) >> 8), 1};
        now += 70;
        sensor_measurement_notify(&bridge, frame, sizeof frame);
        sensor_ml_poll(true);
    }
    assert(runs == before_runs + 1 && direction_window.count == 31);
    assert(strstr(last_published, "\"model\":\"people-counter\""));
    assert(strstr(last_published, "\"input_source\":\"remote:board-a\""));
    /* MQTT can deliver correctly spaced source frames in short bursts. Check
       the source clock for window spacing while retaining local stale checks. */
    sensor_ml_invalidate();
    before_runs = runs;
    uint32_t source_time = 1000;
    for (unsigned i = 0; i < 31; ++i) {
        const uint8_t frame[] = {(uint8_t)(3200 + i), (uint8_t)((3200 + i) >> 8), 1};
        source_time += 70;
        now += 5;
        sensor_measurement_remote_stamp(true, source_time);
        sensor_measurement_notify(&bridge, frame, sizeof frame);
        sensor_measurement_remote_stamp(false, 0);
        sensor_ml_poll(true);
    }
    assert(runs == before_runs + 1 && direction_window.count == 31);
    now += 1000; /* missing a remote frame invalidates the previous window */
    sensor_ml_poll(true);
    assert(direction_window.count == 0 && runs == before_runs + 1);
    sensor_measurement_notify(&bridge, NULL, HAL_ERR_TIMEOUT);
    assert(direction_window.count == 0);
    configs[3] = saved;
}
#endif

/* D6 tasks 5.5, 5.6 and 5.10: the ml_setup files the D6 run sends to Board B,
 * and README 5.4's setup on a local sonar, are accepted by the same code the
 * board runs, each as a new generation of the named model. A setup naming a
 * sensor the board does not have is refused and the running model stays. */
static const char *read_setup(const char *path)
{
    static char buf[2048];
    FILE *f = fopen(path, "rb");
    assert(f && "run from the repository root");
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    /* runtime_mtk.c trims a command payload the same way before ml_setup. */
    char *p = buf;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    n = strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r' || p[n - 1] == '\n')) p[--n] = 0;
    return p;
}

static void follow_board_a(void)
{
    static const struct { const char *name, *source; sensor_profile_t profile; } follow[] = {
        {"A_AHT", "AHT", SENSOR_PROFILE_AHT}, {"A_LIGHT", "LIGHT", SENSOR_PROFILE_TSL2561},
        {"A_SONAR_A", "SONAR_A", SENSOR_PROFILE_PULSE}, {"A_SONAR_B", "SONAR_B", SENSOR_PROFILE_PULSE},
    };
    for (int i = 0; i < 4; ++i) {
        memset(&configs[i], 0, sizeof configs[i]);
        strcpy(configs[i].name, follow[i].name);
        strcpy(configs[i].remote_node, "s3-001");
        strcpy(configs[i].remote_source, follow[i].source);
        configs[i].proto = proto_mqtt;
        configs[i].remote_profile = follow[i].profile;
        configs[i].mode = sample_mode_poll;
        configs[i].timing.mode = TIMING_MODE_PERIODIC;
        configs[i].timing.enabled = true;
        configs[i].timing.interval_ms = 500;
    }
}

static void test_d6_setup_files(void)
{
    static const char *files[][2] = {
        {"config/board_b_sine_float.json", "sine-float"},
        {"config/board_b_sine_int8.json", "sine-int8"},
        {"config/board_b_comfort_summer.json", "comfort-summer"},
#ifdef HAVE_PEOPLE_COUNTER_MODEL
        {"config/board_b_people_counter.json", "people-counter"},
#endif
    };
    follow_board_a();
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        uint32_t g = generation;
        status_ok = false;
        assert(sensor_ml_setup_json(read_setup(files[i][0]), true));
        assert(status_ok && enabled && active >= 0 && !strcmp(models[active].id, files[i][1]) && generation == g + 1);
        printf("%s: OK model=%s generation=%lu\n", files[i][0], files[i][1], (unsigned long)generation);
    }

    /* README 5.4.6: sine models on the multi-bus board's own sonar. */
    memset(&configs[0], 0, sizeof configs[0]);
    strcpy(configs[0].name, "SONAR");
    configs[0].proto = proto_gpio;
    configs[0].gpio_mode = "pulse";
    configs[0].mode = sample_mode_poll;
    configs[0].timing.mode = TIMING_MODE_PERIODIC;
    configs[0].timing.enabled = true;
    configs[0].timing.interval_ms = 500;
    static const char *local[] = {
        "{\"ml_setup\":{\"version\":1,\"model\":\"sine-int8\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"local\",\"name\":\"SONAR\"},\"field\":\"distance_cm\",\"min\":2,\"max\":400}],\"output\":{\"kind\":\"publish\"}}}",
        "{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"local\",\"name\":\"SONAR\"},\"field\":\"distance_cm\",\"min\":2,\"max\":400}],\"output\":{\"kind\":\"publish\"}}}",
    };
    for (size_t i = 0; i < 2; ++i) {
        uint32_t g = generation;
        status_ok = false;
        assert(sensor_ml_setup_json(local[i], true) && status_ok && generation == g + 1);
        assert(!strcmp(models[active].id, i ? "sine-float" : "sine-int8"));
    }
    /* Without a sensor named SONAR the setup is refused and sine-float keeps running. */
    strcpy(configs[0].name, "NOT_SONAR");
    uint32_t g = generation;
    status_ok = true;
    assert(sensor_ml_setup_json(local[0], true));
    assert(!status_ok && generation == g && enabled && !strcmp(models[active].id, "sine-float"));
    puts("README 5.4 local sonar setups: OK sine-int8, OK sine-float; refused without SONAR, previous model kept");
}

int main(void) {
    strcpy(configs[0].name,"AHT"); configs[0].proto=proto_i2c; configs[0].i2c_sensor=I2C_SENSOR_AHT;
    strcpy(configs[1].name,"LIGHT"); configs[1].proto=proto_i2c; configs[1].i2c_sensor=I2C_SENSOR_TSL2561;
    strcpy(configs[2].name,"DISTANCE"); configs[2].proto=proto_gpio; configs[2].gpio_mode="pulse";
    /* Board A's AHT, followed over MQTT and decoded by its remote_profile. */
    strcpy(configs[3].name,"IN_AHT"); configs[3].proto=proto_mqtt; configs[3].remote_profile=SENSOR_PROFILE_AHT;
    strcpy(configs[3].remote_node,"board-a"); strcpy(configs[3].remote_source,"AHT");
    assert(!sensor_ml_command("HELP",false));
    cmd("ML PRESET"); assert(active==0 && bound_count()==4 && !enabled);
    cmd("ML RUN"); assert(enabled); tick(); assert(runs==0);
    samples(); tick(); assert(runs==1);
    float expected=((35.0f/60)+(0.5f)+0+((34.3f-2)/398))*6.28318530718f/4;
    assert(fabsf(seen[0]-expected)<0.0001f); // includes valid zero lux
    sensor_measurement_notify(&configs[2],NULL,HAL_ERR_TIMEOUT); tick(); assert(runs==1);
    samples(); tick(); assert(runs==2);
    now+=6000; sensor_ml_poll(true); assert(runs==2); // stale
    samples(); now+=2600; sensor_measurement_notify(&configs[0],aht,6); sensor_ml_poll(true); assert(runs==2); // skew
    cmd("ML MODEL sine-int8"); assert(active==1 && !enabled); cmd("ML RUN"); samples(); tick(); assert(runs==3);
    rev++; tick(); assert(runs==3 && !enabled); // external model replacement
    cmd("ML PRESET"); cmd("ML RESET"); cmd("ML BIND 0 DISTANCE distance_cm 2 400"); cmd("ML RUN"); samples(); tick(); assert(runs==4);
    assert(fabsf(seen[0]-((34.3f-2)/398)*6.28318530718f)<0.0001f);
    sensor_ml_invalidate(); cmd("ML BIND 0 LIGHT light_lux 0 1000"); cmd("ML RUN"); samples(); tick(); assert(runs==5 && seen[0]==0); // cross-bus substitution
    uint32_t g=generation;
    cmd("ML BIND 0 AHT distance_cm 0 10"); assert(generation==g);
    cmd("ML BIND 0 unknown temperature_c 0 10"); assert(generation==g);
    cmd("ML BIND 0 AHT temperature_c nan 10"); assert(generation==g);
    cmd("ML BIND 0 AHT temperature_c 10 10"); assert(generation==g);
    cmd("ML TIMING 0 1000 100"); assert(generation==g);
    assert(sensor_ml_command("ML MODEL sine-float",true)); assert(active==0); // rejected while sampling
    cmd("ML PRESET autoencoder"); assert(active==2 && bound_count()==16);
    cmd("ML RUN"); samples(); tick(); assert(runs==6 && dim==16 && fabsf(seen[0]-35.0f/60)<0.0001f && seen[2]==0);
    cmd("ML UNBIND 7"); cmd("ML RUN"); assert(!enabled);
    cmd("ML PRESET"); fail_loads=1; cmd("ML MODEL sine-int8"); assert(!enabled && active==0 && ready);
    cmd("ML MODEL sine-float"); cmd("ML RUN"); samples(); fail_invoke=true; tick(); assert(runs==7);
    fail_invoke=false; tick(); assert(runs==8);
    sensor_ml_invalidate(); tick(); assert(runs==8);
    // wrap-safe sample ages, with fresh zero-millisecond samples allowed
    now=UINT32_MAX-100; cmd("ML PRESET"); cmd("ML RUN"); samples(); now=100; sensor_ml_poll(true); assert(runs==9);
    cmd("ML RESET"); cmd("ML BIND 0 LIGHT light_lux 0 1000"); cmd("ML RUN");
    assert(sensor_ml_command("ML THRESHOLD -0.5", true));
    assert(sensor_ml_command("ML THRESHOLD nan", true));
    assert(sensor_ml_command("ML THRESHOLD 1e99", true));
    assert(sensor_ml_command("ML THRESHOLD 1 extra", true));
    samples(); tick();
    assert(strstr(last_line, "\"threshold\":-0.5,\"threshold_exceeded\":true"));
    assert(sensor_ml_command("ML THRESHOLD 0", true));
    tick();
    assert(strstr(last_line, "\"threshold\":0,\"threshold_exceeded\":false"));
    cmd("ML THRESHOLD OFF"); samples(); tick();
    assert(strstr(last_line, "\"status\":\"OK\"") && !strstr(last_line, "threshold"));
    cmd("ML THRESHOLD 0.5");
    cmd("ML PRESET autoencoder"); cmd("ML RUN"); samples(); tick();
    assert(strstr(last_line, "\"status\":\"OK\"") && !strstr(last_line, "threshold"));
    cmd("ML PRESET sine-int8"); cmd("ML RUN");
    last_line[0] = '\0'; tick();
    assert(!last_line[0]); // No successful result or threshold decision without fresh samples.
    cmd("ML PRESET comfort-summer"); assert(dim == 2 && output_dim == 100);
    cmd("ML REPLAY 26.3 67"); assert(fabsf(seen[0]-26.3f)<0.001f && seen[1]==67);
    int after_replay = runs;
    cmd("ML REPLAY 26.3 67 3"); cmd("ML REPLAY 26.3 nan"); assert(runs == after_replay);
    cmd("ML RUN"); samples(); tick(); assert(runs == after_replay + 1);
    assert(fabsf(seen[0]-25.0f)<0.001f && fabsf(seen[1]-50.0f)<0.001f);
    cmd("ML PRESET people-counter");
#ifdef HAVE_PEOPLE_COUNTER_MODEL
    assert(dim == 31 && output_dim == 3);
    cmd("ML RUN"); assert(!enabled);
    cmd("ML REPLAY 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31");
    assert(runs == after_replay + 2 && seen[0] == 1 && seen[30] == 31);
    test_direction_window();
#else
    assert(dim == 2 && output_dim == 100 && enabled);
#endif
    cmd("ML MODEL sine-int8"); assert(dim == 1 && output_dim == 1);
    cmd("ML REPLAY 1.57"); assert(fabsf(seen[0] - 1.57f) < 0.001f);
    const char *live = "{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"local\",\"name\":\"AHT\"},\"field\":\"temperature_c\",\"min\":-10,\"max\":50}],\"output\":{\"kind\":\"publish\"}}}";
    assert(sensor_ml_setup_json(live, true));
    assert(active == 0 && enabled && publish_output && status_ok && published_status);
    int previous_results = published_results;
    samples(); tick();
    assert(published_results == previous_results + 1);
    assert(strstr(last_published, "\"model\":\"sine-float\""));
    uint32_t before_bad = generation;
    assert(sensor_ml_setup_json("{\"ml_setup\":{\"version\":1,\"model\":\"missing\",\"inputs\":[],\"output\":{\"kind\":\"publish\"}}}", true));
    assert(!status_ok && generation == before_bad && active == 0 && enabled);
    assert(sensor_ml_setup_json("{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"remote\",\"name\":\"AHT\"},\"field\":\"temperature_c\",\"min\":-10,\"max\":50}],\"output\":{\"kind\":\"publish\"}}}", true));
    assert(generation == before_bad && active == 0 && enabled);
    assert(sensor_ml_setup_json("{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[],\"output\":{\"kind\":\"publish\",}}}", true));
    assert(generation == before_bad && active == 0 && enabled);
    int before_resume = published_results;
    samples(); tick();
    assert(published_results == before_resume + 1);
    assert(strstr(last_published, "\"model\":\"sine-float\""));
    char live_int8[512];
    snprintf(live_int8, sizeof live_int8, "%s", live);
    char *model_name = strstr(live_int8, "sine-float");
    assert(model_name);
    memmove(model_name + 9, model_name + 10, strlen(model_name + 10) + 1);
    memcpy(model_name, "sine-int8", 9);
    assert(sensor_ml_setup_json(live_int8, true));
    assert(active == 1 && enabled && generation == before_bad + 1 && status_ok);
    samples(); tick();
    assert(published_results == previous_results + 3);
    uint32_t before_fail = generation;
    fail_loads=1;
    assert(sensor_ml_setup_json(live, true));
    assert(generation == before_fail && active == 1 && enabled && !status_ok);

    /* D4 3.3: a remote input names the other board and its sensor. Only the
       followed sensor's frames feed it, a stopped feed goes stale, and
       unfollowed, undecoded or disguised remote inputs are refused. */
    const char *remote = "{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"remote\",\"node\":\"board-a\",\"name\":\"AHT\"},\"field\":\"temperature_c\",\"min\":-10,\"max\":50}],\"output\":{\"kind\":\"publish\"}}}";
    uint32_t g_remote = generation;
    assert(sensor_ml_setup_json(remote, true));
    assert(status_ok && generation == g_remote + 1 && active == 0 && !strcmp(bindings[0].source, "IN_AHT"));
    int r_remote = runs, p_remote = published_results;
    samples(); tick();
    assert(runs == r_remote);                       /* this board's AHT is not board A's */
    sensor_measurement_emit(&configs[3], aht, 6); tick();
    assert(runs == r_remote + 1 && published_results == p_remote + 1);
    assert(fabsf(seen[0] - (35.0f/60)*6.28318530718f) < 0.0001f);
    assert(strstr(last_published, "\"input_source\":\"remote:board-a\""));
    now += 6000; sensor_ml_poll(true); assert(runs == r_remote + 1);   /* feed stopped: stale */
    char bad[600];
    uint32_t g_kept = generation;
    snprintf(bad, sizeof bad, "%s", remote);
    memcpy(strstr(bad, "board-a"), "board-z", 7);
    assert(sensor_ml_setup_json(bad, true) && !status_ok && generation == g_kept);
    snprintf(bad, sizeof bad, "%s", remote);
    char *field = strstr(bad, "temperature_c");
    memmove(field + 9, field + 13, strlen(field + 13) + 1);
    memcpy(field, "light_lux", 9);
    assert(sensor_ml_setup_json(bad, true) && !status_ok && generation == g_kept);
    assert(sensor_ml_setup_json("{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"local\",\"name\":\"IN_AHT\"},\"field\":\"temperature_c\",\"min\":-10,\"max\":50}],\"output\":{\"kind\":\"publish\"}}}", true));
    assert(!status_ok && generation == g_kept);
    assert(sensor_ml_setup_json("{\"ml_setup\":{\"version\":1,\"model\":\"sine-float\",\"inputs\":[{\"index\":0,\"source\":{\"kind\":\"remote\",\"name\":\"AHT\"},\"field\":\"temperature_c\",\"min\":-10,\"max\":50}],\"output\":{\"kind\":\"publish\"}}}", true));
    assert(!status_ok && generation == g_kept && !strcmp(bindings[0].source, "IN_AHT"));
    assert(sensor_ml_setup_json(live, true) && status_ok && generation == g_kept + 1);   /* back to local */
    samples(); tick();
    assert(strstr(last_published, "\"input_source\":\"sensors\""));

    /* D4 3.4: output "local" hands each result to this board's rules as the
       sensor ML: outputs times 1000 as s16le, a class model's index as u16le. */
    char local_setup[600];
    snprintf(local_setup, sizeof local_setup, "%s", live);
    memcpy(strstr(local_setup, "\"publish\""), "\"local\"  ", 9);
    char *pad = strstr(local_setup, "\"local\"  ");
    memmove(pad + 7, pad + 9, strlen(pad + 9) + 1);
    assert(sensor_ml_setup_json(local_setup, true) && status_ok && local_output && !publish_output);
    int p_local = published_results, f_local = local_frames;
    samples(); tick();
    assert(published_results == p_local && local_frames == f_local + 1 && local_len == 2);
    assert(local_s16(0) == (int16_t)lroundf(seen[0] * 1000.0f));
    assert(sensor_ml_command("ML OUTPUT console", true) && !local_output && !publish_output);
    samples(); tick(); assert(local_frames == f_local + 1);
    assert(sensor_ml_command("ML OUTPUT somewhere", true) && !local_output);
    assert(sensor_ml_command("ML OUTPUT local", true) && local_output);
    cmd("ML PRESET comfort-summer"); assert(!local_output);
    cmd("ML OUTPUT local"); cmd("ML REPLAY 26.3 67");
    assert(local_len == 2 && local_frame[0] == 78 && local_frame[1] == 0);   /* comfort score */

    cmd("ML OUTPUT publish");
    cmd("ML REPLAY 26.3 67");
    assert(comfort_updates == 0); /* Replays stay off the live dashboard channel. */
    cmd("ML RUN");
    samples(); tick();
    assert(comfort_updates == 1 && last_comfort_score == 78);
    now += 5001;
    sensor_ml_poll(true);
    assert(comfort_updates == 1); /* Stale inference cannot refresh C's deadline. */

    /* D4 3.2: the model sent over MQTT is chosen by name; its shape is read
       on loading, and a setup that does not fit keeps the previous model. */
    const int uploaded = (int)(sizeof models / sizeof models[0]) - 1;
    cmd("ML MODEL uploaded"); assert(active != uploaded);                 /* nothing uploaded yet */
    assert(sensor_ml_setup_json("{\"ml_setup\":{\"version\":1,\"model\":\"uploaded\",\"inputs\":[],\"output\":{\"kind\":\"console\"}}}", true));
    assert(!status_ok);
    uploaded_size = sizeof uploaded_model;
    cmd("ML MODEL uploaded"); assert(active == uploaded && dim == 3 && output_dim == 3);
    cmd("ML RESET");
    cmd("ML BIND 0 AHT temperature_c -10 50"); cmd("ML BIND 1 LIGHT light_lux 0 1000");
    cmd("ML BIND 2 DISTANCE distance_cm 2 400"); cmd("ML RUN"); assert(enabled);
    int r_up = runs;
    samples(); tick();
    assert(runs == r_up + 1 && fabsf(seen[0] - 35.0f/60) < 0.0001f && seen[1] == 0);
    assert(strstr(last_line, "\"model\":\"uploaded\""));
    cmd("ML UNBIND 2"); cmd("ML RUN"); assert(!enabled);                  /* 2 inputs for 3 */
    cmd("ML REPLAY 1 2 3"); assert(seen[0] == 1 && seen[2] == 3);
    assert(sensor_ml_setup_json(live, true) && status_ok && active == 0);
    uint32_t g_up = generation;
    char up_setup[600];
    snprintf(up_setup, sizeof up_setup, "%s", live);
    char *name = strstr(up_setup, "sine-float");
    memmove(name + 8, name + 10, strlen(name + 10) + 1);
    memcpy(name, "uploaded", 8);
    assert(sensor_ml_setup_json(up_setup, true));
    assert(!status_ok && active == 0 && enabled && generation == g_up);  /* 1 input for 3: restored */
    r_up = runs; samples(); tick(); assert(runs == r_up + 1 && dim == 1);
    uploaded_dim = 1; uploaded_out = 1;
    assert(sensor_ml_setup_json(up_setup, true));
    assert(status_ok && active == uploaded && generation == g_up + 1);
    r_up = runs; samples(); tick();
    assert(runs == r_up + 1 && fabsf(seen[0] - 35.0f/60) < 0.0001f);      /* no sine scaling */
    test_d6_setup_files();
    puts("sensor ML integration tests PASS (interpreter boundary simulated)");
}
