/* Optional device protocols using the shared HAL and scheduler pending API.
 * One SchedulerTask owns each context. No OS calls, sleeps or held bus
 * transactions across polls; conversion waits do not block other sensors.
 */
#include "bus/i2c_profiles.h"
#include "bus/bus_if.h"
#include <math.h>
#include <stdlib.h>

enum { IDLE, AHT_INIT1, AHT_INIT2, AHT_CONVERT, TSL_LOW, TSL_HIGH, AHT_STATUS };
struct i2c_profile {
    i2c_sensor_t sensor;
    int bus;
    uint8_t address, state, part;
    uint64_t ready_us, deadline_us;
};

static int write_bytes(i2c_profile_t *p,const uint8_t *data,size_t n) {
    int rc=hal_i2c_write(p->bus,p->address,data,n,false);
    return rc==(int)n ? HAL_OK : (rc<0 ? rc : HAL_ERR_IO);
}
static int read_bytes(i2c_profile_t *p,uint8_t *data,size_t n) {
    int rc=hal_i2c_read(p->bus,p->address,data,n);
    return rc==(int)n ? HAL_OK : (rc<0 ? rc : HAL_ERR_IO);
}
static int reg_read(i2c_profile_t *p,uint8_t reg,uint8_t *data,size_t n) {
    int rc=hal_i2c_write(p->bus,p->address,&reg,1,true);
    if (rc!=1) return rc<0 ? rc : HAL_ERR_IO;
    return read_bytes(p,data,n); // complete repeated-start in this call
}
static int reg_write(i2c_profile_t *p,uint8_t reg,uint8_t value) {
    const uint8_t data[]={reg,value}; return write_bytes(p,data,sizeof data);
}
static int aht_command(i2c_profile_t *p,uint8_t cmd,uint8_t argument) {
    const uint8_t bytes[]={cmd,argument,0};
    return write_bytes(p,bytes,sizeof bytes); // one complete three-byte packet
}
static int defer(i2c_profile_t *p,uint8_t state,uint32_t ms) {
    p->state=state; p->ready_us=hal_time_us()+(uint64_t)ms*1000u;
    return BUS_POLL_PENDING;
}
static int finish(i2c_profile_t *p,int result) { p->state=IDLE; return result; }

int i2c_profile_create(const app_cfg_t *cfg,int bus,i2c_profile_t **out) {
    if (!cfg || !out || bus<0 || (cfg->i2c_sensor!=I2C_SENSOR_AHT && cfg->i2c_sensor!=I2C_SENSOR_TSL2561))
        return HAL_ERR_INVALID;
    *out=NULL;
    if (cfg->mode!=sample_mode_poll || cfg->i2c_pre_len || cfg->i2c_post_delay_ms ||
        (cfg->i2c_sensor==I2C_SENSOR_AHT && cfg->i2c_addr!=0x38) ||
        (cfg->i2c_sensor==I2C_SENSOR_TSL2561 && cfg->i2c_addr!=0x29 && cfg->i2c_addr!=0x39 && cfg->i2c_addr!=0x49))
        return HAL_ERR_INVALID;
    i2c_profile_t *p=calloc(1,sizeof *p);
    if (!p) return HAL_ERR_NO_MEMORY;
    p->sensor=cfg->i2c_sensor; p->bus=bus; p->address=(uint8_t)cfg->i2c_addr;
    *out=p; return HAL_OK; // missing device remains configured for later retries
}

static int aht_poll(i2c_profile_t *p,uint8_t *data) {
    uint8_t status=0;
    int rc=read_bytes(p,&status,1);
    if (rc!=HAL_OK) return finish(p,rc);
    if (status&0x80) return defer(p,p->state==IDLE ? AHT_STATUS : p->state,10);
    if (!(status&0x08)) {
        if (p->state==IDLE || p->state==AHT_STATUS) {
            rc=aht_command(p,0xE1,0x08);
            /* An AHT20 may reject AHT10's init command. Try its own command. */
            if (rc!=HAL_OK) {
                rc=aht_command(p,0xBE,0x08);
                return rc==HAL_OK ? defer(p,AHT_INIT2,20) : finish(p,rc);
            }
            return defer(p,AHT_INIT1,20);
        }
        if (p->state==AHT_INIT1) {
            rc=aht_command(p,0xBE,0x08);
            return rc==HAL_OK ? defer(p,AHT_INIT2,20) : finish(p,rc);
        }
        return finish(p,HAL_ERR_VERIFY);
    }
    if (p->state==AHT_CONVERT) {
        rc=read_bytes(p,data,6);
        if (rc!=HAL_OK) return finish(p,rc);
        if ((data[0]&0x80) || !(data[0]&0x08)) return finish(p,HAL_ERR_VERIFY);
        return finish(p,6);
    }
    rc=aht_command(p,0xAC,0x33);
    return rc==HAL_OK ? defer(p,AHT_CONVERT,80) : finish(p,rc);
}

float i2c_tsl_lux(uint16_t broadband,uint16_t infrared,bool gain16,uint8_t part) {
    if (!broadband) return 0;
    const float scale=(322.0f/81.0f)*(gain16 ? 1.0f : 16.0f);
    const float ch0=broadband*scale,ch1=infrared*scale,ratio=ch1/ch0;
    float lux=0;
    if (part==0x10) {
        if (ratio<=0.52f) lux=0.0315f*ch0-0.0593f*ch0*powf(ratio,1.4f);
        else if (ratio<=0.65f) lux=0.0229f*ch0-0.0291f*ch1;
        else if (ratio<=0.80f) lux=0.0157f*ch0-0.0180f*ch1;
        else if (ratio<=1.30f) lux=0.00338f*ch0-0.00260f*ch1;
    } else {
        if (ratio<=0.50f) lux=0.0304f*ch0-0.062f*ch0*powf(ratio,1.4f);
        else if (ratio<=0.61f) lux=0.0224f*ch0-0.031f*ch1;
        else if (ratio<=0.80f) lux=0.0128f*ch0-0.0153f*ch1;
        else if (ratio<=1.30f) lux=0.00146f*ch0-0.00112f*ch1;
    }
    return lux>0 ? lux : 0;
}
static int tsl_begin(i2c_profile_t *p,bool gain16) {
    int rc=reg_write(p,0x80,0); // restart integration with selected gain
    if (rc==HAL_OK) rc=reg_write(p,0x81,gain16 ? 0x11 : 0x01);
    if (rc==HAL_OK) rc=reg_write(p,0x80,3);
    return rc==HAL_OK ? defer(p,gain16 ? TSL_HIGH : TSL_LOW,120) : finish(p,rc);
}
static int tsl_poll(i2c_profile_t *p,uint8_t *data) {
    if (p->state==IDLE) {
        uint8_t id;
        int rc=reg_read(p,0x8A,&id,1);
        if (rc!=HAL_OK) return finish(p,rc);
        p->part=id&0xF0;
        if (p->part!=0x10 && p->part!=0x50) return finish(p,HAL_ERR_VERIFY);
        return tsl_begin(p,false);
    }
    uint8_t raw[4];
    int rc=reg_read(p,0xAC,raw,2);
    if (rc==HAL_OK) rc=reg_read(p,0xAE,raw+2,2);
    int off=reg_write(p,0x80,0);
    if (rc!=HAL_OK) return finish(p,rc);
    if (off!=HAL_OK) return finish(p,off);
    uint16_t ch0=(uint16_t)raw[0]|((uint16_t)raw[1]<<8);
    uint16_t ch1=(uint16_t)raw[2]|((uint16_t)raw[3]<<8);
    if (ch0>37000u || ch1>37000u) return finish(p,HAL_ERR_VERIFY);
    if (p->state==TSL_LOW && ch0<200u) return tsl_begin(p,true);
    uint32_t lux=(uint32_t)(i2c_tsl_lux(ch0,ch1,p->state==TSL_HIGH,p->part)+0.5f);
    for (unsigned i=0;i<4;++i) { data[i]=raw[i]; data[i+4]=(uint8_t)(lux>>(8*i)); }
    return finish(p,8);
}
int i2c_profile_poll(i2c_profile_t *p,uint8_t *data,size_t cap) {
    if (!p || !data || cap<(p->sensor==I2C_SENSOR_AHT ? 6u : 8u)) return HAL_ERR_INVALID;
    uint64_t now=hal_time_us();
    if (p->state==IDLE) p->deadline_us=now+600000u;
    else if (now>=p->deadline_us) { i2c_profile_cancel(p); return HAL_ERR_TIMEOUT; }
    else if (now<p->ready_us) return BUS_POLL_PENDING;
    return p->sensor==I2C_SENSOR_AHT ? aht_poll(p,data) : tsl_poll(p,data);
}
void i2c_profile_cancel(i2c_profile_t *p) {
    if (!p) return;
    if (p->sensor==I2C_SENSOR_TSL2561 && p->state!=IDLE) (void)reg_write(p,0x80,0);
    p->state=IDLE;
}
void i2c_profile_free(i2c_profile_t *p) { i2c_profile_cancel(p); free(p); }
