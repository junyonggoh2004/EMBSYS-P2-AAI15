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
    if (aht) {
        okay=data && length==6 && !(data[0]&0x80) && (data[0]&0x08);
        if (okay) {
            uint32_t rh=((uint32_t)data[1]<<12)|((uint32_t)data[2]<<4)|(data[3]>>4);
            uint32_t temp=((uint32_t)(data[3]&15)<<16)|((uint32_t)data[4]<<8)|data[5];
            snprintf(fields,sizeof fields,"\"temperature_c\":%.2f,\"humidity_pct\":%.2f",
                     temp*200.0/1048576.0-50.0,rh*100.0/1048576.0);
        } else snprintf(fields,sizeof fields,"\"temperature_c\":null,\"humidity_pct\":null");
    } else if (tsl) {
        okay=data && length==8;
        if (okay) snprintf(fields,sizeof fields,"\"light_lux\":%lu",(unsigned long)le32(data+4));
        else snprintf(fields,sizeof fields,"\"light_lux\":null");
    } else {
        okay=data && length==3 && data[2]==1;
        uint32_t width=okay ? ((uint32_t)data[0]|((uint32_t)data[1]<<8)) : 0;
        double cm=width*0.0343/2.0;
        if (okay && (cm<2.0 || cm>400.0)) okay=false;
        if (okay) snprintf(fields,sizeof fields,"\"distance_cm\":%.1f,\"echo_us\":%lu",cm,(unsigned long)width);
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
    char line[320];
    if (sensor_measurement_json(cfg,data,length,line,sizeof line)>0) printf("%s\n",line);
}
