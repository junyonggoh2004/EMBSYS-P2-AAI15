#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bus/i2c_profiles.h"
#include "bus/bus_if.h"
#include "application/sensor_measurement.h"
#include "application/scheduler.h"

static uint64_t clock_us;
static uint8_t aht_status=8, calibration_command=0xE1, tsl_id=0x50, selected_reg;
static uint16_t channels[2]={1000,100};
static unsigned triggers, init1, init2, reads, writes, delays, off_writes;
static int fail_address=-1, short_read;
static bool high_gain, dim_scene;
static const uint8_t aht_frame[]={8,0x80,0,6,0,0}; // 50% RH, 25 C
static unsigned samples[3], pulse_polls;
static int fake_gpio_init(const sensor_cfg_t *cfg,void **out) { (void)cfg; *out=&pulse_polls; pulse_polls=0; return HAL_OK; }
static void fake_gpio_deinit(void *ctx) { (void)ctx; }
static int fake_gpio_poll(void *ctx,uint8_t *data,size_t cap) {
    (void)ctx; assert(cap>=3);
    if (++pulse_polls<3) return BUS_POLL_PENDING;
    data[0]=0xD0; data[1]=7; data[2]=1; return 3;
}
static const bus_t fake_gpio={.name="gpio",.init_cfg=fake_gpio_init,.deinit=fake_gpio_deinit,.poll_once=fake_gpio_poll};
const bus_t *bus_lookup(const char *name) { return !strcmp(name,"i2c") ? &bus_i2c : &fake_gpio; }
bool mqtt_follow_remote_source(const char *node,const char *source,const char *alias)
{ (void)node; (void)source; (void)alias; return false; }
bool mqtt_pub_bytes(const char *source,const uint8_t *data,size_t len,int qos,bool retain)
{ (void)source; (void)data; (void)len; (void)qos; (void)retain; return true; }
bool mqtt_pub_line(const char *source,const char *line,int qos,bool retain)
{ (void)source; (void)line; (void)qos; (void)retain; return true; }
void rules_on_sample(const char *source,const uint8_t *data,int len,uint32_t ts) {
    (void)data; (void)ts;
    unsigned slot=!strcmp(source,"AHT") ? 0 : !strcmp(source,"LIGHT") ? 1 : 2;
    assert(len==(slot==0 ? 6 : slot==1 ? 8 : 3)); samples[slot]++;
}
int hal_gpio_capture_start(int trig,int echo,uint32_t pulse,uint32_t timeout)
{ (void)trig; (void)echo; (void)pulse; (void)timeout; return HAL_ERR_UNSUPPORTED; }
int hal_gpio_capture_poll(int echo,hal_pulse_result_t *result)
{ (void)echo; (void)result; return HAL_ERR_UNSUPPORTED; }
void hal_gpio_capture_cancel(int echo) { (void)echo; }
uint64_t hal_time_us(void) { return clock_us; }
uint32_t hal_time_ms(void) { return (uint32_t)(clock_us/1000u); }
void hal_delay_ms(uint32_t ms) { delays++; clock_us+=(uint64_t)ms*1000u; }
bool hal_gpio_valid(int pin) { return pin>=0 && pin<19; }
int hal_i2c_open(int sda,int scl,uint32_t baud) { assert(sda==1 && scl==2 && baud==100000); return 0; }
int hal_i2c_write(int bus,uint8_t addr,const uint8_t *data,size_t len,bool nostop) {
    assert(bus==0); writes++;
    if (addr==fail_address) return HAL_ERR_IO;
    if (addr==0x38) {
        assert(len==3 && !nostop && data[2]==0);
        if (data[0]==0xAC) { assert(data[1]==0x33); triggers++; }
        else {
            assert(data[1]==8);
            if (data[0]==0xE1) init1++; else { assert(data[0]==0xBE); init2++; }
            if (data[0]==calibration_command) aht_status=8;
        }
    } else {
        assert(addr==0x29);
        if (len==1) { assert(nostop); selected_reg=data[0]; }
        else {
            assert(len==2 && !nostop);
            if (data[0]==0x81) high_gain=(data[1]&0x10)!=0;
            if (data[0]==0x80 && data[1]==0) off_writes++;
        }
    }
    return (int)len;
}
int hal_i2c_read(int bus,uint8_t addr,uint8_t *data,size_t len) {
    assert(bus==0); reads++;
    if (addr==fail_address) return HAL_ERR_IO;
    if (short_read) { short_read=0; return (int)len-1; }
    if (addr==0x38) {
        if (len==1) data[0]=aht_status;
        else { assert(len==6); memcpy(data,aht_frame,6); }
    } else {
        assert(addr==0x29);
        if (selected_reg==0x8A) { assert(len==1); data[0]=tsl_id; }
        else {
            assert(len==2 || len==4);
            uint16_t ch0=dim_scene && !high_gain ? 10 : channels[0];
            uint16_t value=selected_reg==0xAE ? channels[1] : ch0;
            data[0]=(uint8_t)value; data[1]=(uint8_t)(value>>8);
            if (len==4) { data[2]=(uint8_t)channels[1]; data[3]=(uint8_t)(channels[1]>>8); }
        }
    }
    return (int)len;
}
static app_cfg_t config(i2c_sensor_t sensor) {
    app_cfg_t cfg={0};
    cfg.proto=proto_i2c; cfg.mode=sample_mode_poll;
    cfg.i2c_sensor=sensor; cfg.i2c_sda=1; cfg.i2c_scl=2; cfg.i2c_baud=100000;
    cfg.i2c_addr=sensor==I2C_SENSOR_AHT ? 0x38 : 0x29;
    cfg.timing=(timing_config_t){TIMING_MODE_PERIODIC,2000,600,0,true};
    snprintf(cfg.name,sizeof cfg.name,"%s",sensor==I2C_SENSOR_AHT ? "AHT" : "LIGHT");
    return cfg;
}
static void *open_sensor(app_cfg_t *cfg) {
    void *ctx=NULL;
    sensor_cfg_t scfg={cfg->name,"i2c",cfg};
    assert(bus_i2c.init_cfg(&scfg,&ctx)==0 && ctx);
    return ctx;
}
static void test_aht(void) {
    app_cfg_t cfg=config(I2C_SENSOR_AHT); void *ctx=open_sensor(&cfg);
    uint8_t data[16]; char json[320]; unsigned before=delays;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    assert(triggers==1 && delays==before);
    clock_us+=79000; unsigned old_reads=reads;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING && reads==old_reads);
    clock_us+=1000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==6 && !memcmp(data,aht_frame,6));
    assert(sensor_measurement_json(&cfg,data,6,json,sizeof json)>0);
    assert(strstr(json,"\"temperature_c\":25.00") && strstr(json,"\"humidity_pct\":50.00"));
    /* BUSY across calls must not restart the deadline or block the task. */
    aht_status=0x88;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=600000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==HAL_ERR_TIMEOUT);
    aht_status=8; fail_address=0x38;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==HAL_ERR_IO);
    assert(sensor_measurement_json(&cfg,NULL,HAL_ERR_IO,json,sizeof json)>0);
    assert(strstr(json,"\"temperature_c\":null") && !strstr(json,"25.00"));
    fail_address=-1;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    bus_i2c.cancel(ctx); // STOP can restart without returning stale conversion data
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=80000; short_read=1;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==HAL_ERR_IO);
    bus_i2c.deinit(ctx);
}
static void test_aht20_calibration(void) {
    app_cfg_t cfg=config(I2C_SENSOR_AHT); void *ctx=open_sensor(&cfg); uint8_t data[8];
    aht_status=0; calibration_command=0xBE;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=20000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=20000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=80000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==6 && init1==1 && init2==1);
    bus_i2c.deinit(ctx);
}
static void test_light(void) {
    app_cfg_t cfg=config(I2C_SENSOR_TSL2561); void *ctx=open_sensor(&cfg);
    uint8_t data[8]; char json[320]; unsigned before=delays;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=120000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==8 && data[0]==0xE8 && data[1]==3);
    assert(delays==before && sensor_measurement_json(&cfg,data,8,json,sizeof json)>0 && strstr(json,"\"status\":\"OK\""));
    assert(i2c_tsl_lux(1000,100,false,0x50)>1000.0f);
    assert(i2c_tsl_lux(1000,100,true,0x50)>0.0f);
    assert(i2c_tsl_lux(1000,100,true,0x10)>0.0f);
    channels[0]=channels[1]=0; dim_scene=true;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=120000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING && high_gain);
    clock_us+=120000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==8);
    assert(sensor_measurement_json(&cfg,data,8,json,sizeof json)>0 && strstr(json,"\"light_lux\":0"));
    dim_scene=false; channels[0]=38000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=120000;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==HAL_ERR_VERIFY);
    assert(sensor_measurement_json(&cfg,NULL,HAL_ERR_VERIFY,json,sizeof json)>0 && strstr(json,"\"light_lux\":null"));
    channels[0]=1000; tsl_id=0;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==HAL_ERR_VERIFY);
    tsl_id=0x50;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    unsigned off=off_writes; bus_i2c.cancel(ctx); assert(off_writes==off+1);
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==BUS_POLL_PENDING);
    clock_us+=120000; fail_address=0x29;
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==HAL_ERR_IO);
    fail_address=-1; bus_i2c.deinit(ctx);
}
static void test_raw_and_configuration(void) {
    app_cfg_t cfg=config(I2C_SENSOR_RAW); cfg.i2c_reg=0x8C; cfg.i2c_reg_size=1;
    cfg.i2c_read_len=4; cfg.i2c_restart=1;
    const uint8_t pre[]={0x80,3,0x81,2}; memcpy(cfg.i2c_pre,pre,4); cfg.i2c_pre_len=4;
    unsigned before=writes; void *ctx=open_sensor(&cfg); uint8_t data[8];
    assert(writes==before+2); // legacy pre remains two register/value packets
    assert(bus_i2c.poll_once(ctx,data,sizeof data)==4);
    bus_i2c.deinit(ctx);
    cfg=config(I2C_SENSOR_AHT); cfg_parse_begin();
    cfg_parse_line("i2c.sensor=tsl2561",&cfg); cfg_parse_line("i2c.addr=0x29",&cfg);
    cfg_parse_line("i2c.baud=100000",&cfg); cfg_parse_end(&cfg);
    assert(!cfg.cfg_error[0] && cfg.i2c_sensor==I2C_SENSOR_TSL2561);
    cfg_parse_begin(); cfg_parse_line("i2c.sensor=unknown",&cfg); cfg_parse_end(&cfg);
    assert(cfg.cfg_error[0]);
    cfg=config(I2C_SENSOR_AHT); cfg.mode=sample_mode_stream;
    cfg_parse_begin(); cfg_parse_end(&cfg); assert(cfg.cfg_error[0]);
    cfg=config(I2C_SENSOR_AHT); cfg.i2c_addr=0x76;
    cfg_parse_begin(); cfg_parse_end(&cfg); assert(cfg.cfg_error[0]);
}
static void test_distance_json(void) {
    app_cfg_t cfg={0}; cfg.proto=proto_gpio; cfg.gpio_mode="pulse";
    snprintf(cfg.name,sizeof cfg.name,"DISTANCE");
    const uint8_t echo[]={0xD0,7,1}; // 2000 us = 34.3 cm
    char json[320];
    assert(sensor_measurement_json(&cfg,echo,3,json,sizeof json)>0 && strstr(json,"\"distance_cm\":34.3"));
    assert(sensor_measurement_json(&cfg,NULL,HAL_ERR_TIMEOUT,json,sizeof json)>0 && strstr(json,"\"distance_cm\":null"));
    const uint8_t invalid[]={0};
    assert(sensor_measurement_json(&cfg,invalid,1,json,sizeof json)>0 && strstr(json,"\"distance_cm\":null"));
    assert(sensor_measurement_json(&cfg,echo,3,json,5)==0); // truncated output is not emitted
}
static void add_three(void) {
    app_cfg_t aht=config(I2C_SENSOR_AHT),light=config(I2C_SENSOR_TSL2561),sonar={0};
    sonar.proto=proto_gpio; sonar.mode=sample_mode_poll; sonar.gpio_mode="pulse";
    sonar.gpio_trig=4; sonar.gpio_echo=5;
    sonar.timing=aht.timing; snprintf(sonar.name,sizeof sonar.name,"DISTANCE");
    assert(scheduler_add_config(&aht)==0 && scheduler_add_config(&light)==1 && scheduler_add_config(&sonar)==2);
}
static void test_scheduler_integration(void) {
    clock_us=1000000; aht_status=8; channels[0]=1000; channels[1]=100;
    scheduler_reset(); memset(samples,0,sizeof samples); add_three();
    unsigned before=delays;
    for (int i=0;i<=120;++i) { scheduler_run_all(true); clock_us+=1000; }
    assert(samples[0]==1 && samples[1]==1 && samples[2]==1 && delays==before);
    clock_us=3000000; scheduler_run_all(true); // both conversions now pending
    unsigned off=off_writes, trigger_count=triggers;
    scheduler_run_all(false); assert(off_writes==off+1);
    scheduler_run_all(true); assert(triggers==trigger_count+1); // restart, not stale delivery
    scheduler_reset();
    memset(samples,0,sizeof samples); add_three(); fail_address=0x38;
    for (int i=0;i<=120;++i) { scheduler_run_all(true); clock_us+=1000; }
    assert(samples[0]==0 && samples[1]==1 && samples[2]==1); // AHT failure does not stop peers or fire a sample rule
    fail_address=-1; clock_us+=2000000;
    for (int i=0;i<=120;++i) { scheduler_run_all(true); clock_us+=1000; }
    assert(samples[0]==1 && samples[1]==2 && samples[2]==2);
    scheduler_reset(); assert(scheduler_count()==0);
    puts("Three-sensor scheduler integration: PASS (interleaving, independent faults, recovery, STOP/CLEAR lifecycle, valid-frame rules)");
}
int main(void) {
    test_aht(); test_aht20_calibration(); test_light(); test_raw_and_configuration(); test_distance_json();
    test_scheduler_integration();
    puts("I2C sensor profiles: PASS (full command packets, calibration, pending waits, deadlines, failures/recovery, STOP, raw compatibility, lux/null decoding)");
    return 0;
}
