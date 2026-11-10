#include "application/sensor_measurement.h"
#include <stdio.h>
#include <string.h>

static bool json_name(const char *name,char *out,size_t cap) {
    size_t used=0;
    for (size_t i=0;i<32 && name[i];++i) {
        unsigned char ch=(unsigned char)name[i];
        if (ch<0x20 || ch>=0x7f) return false;
        if (ch=='"' || ch=='\\') { if (used+2>=cap) return false; out[used++]='\\'; }
        if (used+1>=cap) return false;
        out[used++]=(char)ch;
    }
    out[used]=0; return true;
}
static uint32_t le32(const uint8_t *data) {
    return (uint32_t)data[0]|((uint32_t)data[1]<<8)|((uint32_t)data[2]<<16)|((uint32_t)data[3]<<24);
}
bool sensor_measurement_field_supported(const app_cfg_t *c,const char *f) {
    if(!c || !f) return false;
    if(c->proto==proto_i2c && c->i2c_sensor==I2C_SENSOR_AHT)
        return !strcmp(f,"temperature_c") || !strcmp(f,"humidity_pct");
    if(c->proto==proto_i2c && c->i2c_sensor==I2C_SENSOR_TSL2561) return !strcmp(f,"light_lux");
    return c->proto==proto_gpio && c->gpio_mode && !strcmp(c->gpio_mode,"pulse") && !strcmp(f,"distance_cm");
}
bool sensor_measurement_value(const app_cfg_t *c,const uint8_t *d,int n,const char *f,float *v) {
    if(!v || !d || !sensor_measurement_field_supported(c,f)) return false;
    if(c->proto==proto_i2c && c->i2c_sensor==I2C_SENSOR_AHT) {
        if(n!=6 || (d[0]&0x80) || !(d[0]&8)) return false;
        uint32_t rh=((uint32_t)d[1]<<12)|((uint32_t)d[2]<<4)|(d[3]>>4);
        uint32_t t=((uint32_t)(d[3]&15)<<16)|((uint32_t)d[4]<<8)|d[5];
        *v=!strcmp(f,"temperature_c") ? t*200.0f/1048576.0f-50.0f : rh*100.0f/1048576.0f;
    } else if(c->proto==proto_i2c) {
        if(n!=8) return false;
        *v=(float)le32(d+4);
    } else {
        if(n!=3 || d[2]!=1) return false;
        *v=((uint32_t)d[0]|((uint32_t)d[1]<<8))*0.0343f/2.0f;
        if(*v<2 || *v>400) return false;
    }
    return true;
}
/* Optional platform subscriber; other targets retain their existing behavior. */
__attribute__((weak)) void sensor_measurement_notify(const app_cfg_t *c,const uint8_t *d,int n) {
    (void)c; (void)d; (void)n;
}
int sensor_measurement_json(const app_cfg_t *cfg,const uint8_t *data,int length,char *out,size_t cap) {
    if (!cfg || !out || !cap) return 0;
    bool aht=cfg->proto==proto_i2c && cfg->i2c_sensor==I2C_SENSOR_AHT;
    bool tsl=cfg->proto==proto_i2c && cfg->i2c_sensor==I2C_SENSOR_TSL2561;
    bool pulse=cfg->proto==proto_gpio && cfg->gpio_mode && !strcmp(cfg->gpio_mode,"pulse");
    if (!aht && !tsl && !pulse) return 0;
    char name[65];
    if (!json_name(cfg->name,name,sizeof name)) return 0;
    char fields[140];
    bool okay=false;
    float v,w;
    if (aht) {
        okay=sensor_measurement_value(cfg,data,length,"temperature_c",&v) && sensor_measurement_value(cfg,data,length,"humidity_pct",&w);
        if(okay) snprintf(fields,sizeof fields,"\"temperature_c\":%.2f,\"humidity_pct\":%.2f",(double)v,(double)w);
        else snprintf(fields,sizeof fields,"\"temperature_c\":null,\"humidity_pct\":null");
    } else if(tsl) {
        okay=sensor_measurement_value(cfg,data,length,"light_lux",&v);
        if(okay) snprintf(fields,sizeof fields,"\"light_lux\":%lu",(unsigned long)le32(data+4));
        else snprintf(fields,sizeof fields,"\"light_lux\":null");
    } else {
        okay=sensor_measurement_value(cfg,data,length,"distance_cm",&v);
        if(okay) snprintf(fields,sizeof fields,"\"distance_cm\":%.1f,\"echo_us\":%lu",(double)v,(unsigned long)((uint32_t)data[0]|((uint32_t)data[1]<<8)));
        else snprintf(fields,sizeof fields,"\"distance_cm\":null,\"echo_us\":null");
    }
    const char *status=okay ? "OK" : length==HAL_ERR_TIMEOUT ? "TIMEOUT" :
                       length==HAL_ERR_VERIFY ? "INVALID_RESPONSE_OR_SATURATED" :
                       length<0 ? "SENSOR_ERROR_OR_MISSING" : "INVALID_READING";
    int n=snprintf(out,cap,"{\"sensor\":\"%s\",\"timestamp_ms\":%lu,%s,\"status\":\"%s\",\"error\":%d}",
                   name,(unsigned long)hal_time_ms(),fields,status,length<0 ? length : 0);
    return n>0 && (size_t)n<cap ? n : 0;
}
void sensor_measurement_emit(const app_cfg_t *cfg,const uint8_t *data,int length) {
    if(cfg) sensor_measurement_notify(cfg,data,length);
    char line[320];
    if (sensor_measurement_json(cfg,data,length,line,sizeof line)>0) printf("%s\n",line);
}
