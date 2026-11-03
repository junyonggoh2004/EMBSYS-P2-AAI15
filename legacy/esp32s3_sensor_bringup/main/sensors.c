#include "sensors.h"
#include "platform.h"
#include "app_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#define AHT_ADDRESS 0x38
#define TRIG_PIN ULTRASONIC_TRIG
#define ECHO_PIN ULTRASONIC_ECHO
static int ahtI2cError,lightI2cError;
static const char *ahtStatus="NOT_FOUND",*lightStatus="NOT_FOUND";
static uint8_t lightAddress,lightPart;
static bool lightFound;
static bool readBytes(uint8_t *data,size_t count) {
 ahtI2cError=platform_i2c(AHT_ADDRESS,NULL,0,data,count);
 if (ahtI2cError) ahtStatus="I2C_READ_ERROR";
 return ahtI2cError==0;
}

static bool command(uint8_t cmd,uint8_t arg1,uint8_t arg2) {
 uint8_t bytes[]={cmd,arg1,arg2};
 ahtI2cError=platform_i2c(AHT_ADDRESS,bytes,3,NULL,0);
 if (ahtI2cError) ahtStatus="I2C_WRITE_ERROR";
 return ahtI2cError==0;
}

bool waitReady(uint8_t *state) {
  const uint32_t started = platform_millis();
  do {
    if (!readBytes(state, 1)) return false;
    if (!(*state & 0x80)) return true;
    platform_sleep_ms(10);
  } while ((uint32_t)(platform_millis()-started) < 500);
  ahtStatus = "BUSY_TIMEOUT";
  return false;
}

bool measureAht(float *temperature, float *humidity) {
  ahtI2cError=platform_i2c(AHT_ADDRESS,NULL,0,NULL,0);
  if (ahtI2cError != 0) {
    ahtStatus = "NOT_FOUND";
    return false;
  }

  uint8_t state = 0;
  if (!waitReady(&state)) return false;
  if (!(state & 0x08)) {
    // AHT10 initialization, then AHT20 initialization if necessary.
    const uint8_t initCommands[] = {0xE1, 0xBE};
    bool initialized = false;
    for (size_t init=0; init<sizeof(initCommands); ++init) {
      uint8_t cmd=initCommands[init];
      command(cmd, 0x08, 0x00);
      platform_sleep_ms(20);
      if (waitReady(&state) && (state & 0x08)) {
        initialized = true;
        break;
      }
    }
    if (!initialized) {
      ahtStatus = "INIT_FAILED";
      return false;
    }
  }

  if (!command(0xAC, 0x33, 0x00)) return false;
  platform_sleep_ms(80);
  if (!waitReady(&state)) return false;

  uint8_t data[6];
  if (!readBytes(data, sizeof(data))) return false;
  if ((data[0] & 0x80) || !(data[0] & 0x08)) {
    ahtStatus = "INVALID_STATUS";
    return false;
  }

  const uint32_t rawHumidity = ((uint32_t)(data[1]) << 12) |
      ((uint32_t)(data[2]) << 4) | (data[3] >> 4);
  const uint32_t rawTemperature = ((uint32_t)(data[3] & 0x0F) << 16) |
      ((uint32_t)(data[4]) << 8) | data[5];
  *humidity = rawHumidity * 100.0f / 1048576.0f;
  *temperature = rawTemperature * 200.0f / 1048576.0f - 50.0f;
  ahtStatus = "OK";
  return true;
}


static bool measureDistance(float *cm,uint32_t *echoUs,const char **distanceStatus) {
    *echoUs=0;
    uint32_t started=platform_millis();
    while (platform_gpio_read(ECHO_PIN) && (uint32_t)(platform_millis()-started)<100) platform_sleep_ms(1);
    if (platform_gpio_read(ECHO_PIN)) { *distanceStatus="ECHO_STUCK_HIGH"; return false; }
    platform_gpio_write(TRIG_PIN,false); platform_delay_us(3);
    platform_gpio_write(TRIG_PIN,true); platform_delay_us(10);
    platform_gpio_write(TRIG_PIN,false);
    uint32_t cycles=platform_cycles();
    while (!platform_gpio_read(ECHO_PIN)) {
        if (platform_elapsed_us(cycles)>=ECHO_TIMEOUT_US) { *distanceStatus="NO_ECHO_OR_MISSING"; return false; }
    }
    uint32_t rise=platform_cycles();
    while (platform_gpio_read(ECHO_PIN)) {
        if (platform_elapsed_us(cycles)>=ECHO_TIMEOUT_US) { *distanceStatus="ECHO_TIMEOUT"; return false; }
    }
    *echoUs=platform_elapsed_us(rise); *cm=*echoUs*0.0343f/2.0f;
    if (*cm<2.0f || *cm>400.0f) { *distanceStatus="OUT_OF_RANGE"; return false; }
    *distanceStatus="OK"; return true;
}

static bool readLightRegister(uint8_t address,uint8_t cmd,uint16_t *value,uint8_t length) {
    uint8_t bytes[2]={0}; *value=0;
    lightI2cError=platform_i2c(address,&cmd,1,bytes,length);
    if (lightI2cError) return false;
    *value=bytes[0]; if (length==2) *value|=(uint16_t)bytes[1]<<8;
    return true;
}
static bool writeLightRegister(uint8_t reg,uint8_t value) {
    uint8_t bytes[]={0x80|reg,value};
    lightI2cError=platform_i2c(lightAddress,bytes,2,NULL,0);
    return lightI2cError==0;
}

bool readLightFrame(bool gain16, uint16_t *broadband, uint16_t *infrared) {
  *broadband = *infrared = 0;
  if (!writeLightRegister(0x01, gain16 ? 0x11 : 0x01)) return false;
  if (!writeLightRegister(0x00, 0x03)) return false;
  platform_sleep_ms(120);
  const bool okay = readLightRegister(lightAddress, 0xAC, broadband, 2) &&
                    readLightRegister(lightAddress, 0xAE, infrared, 2);
  const int readError = lightI2cError;
  const bool stopped = writeLightRegister(0x00, 0x00);
  if (!okay) lightI2cError = readError; // Preserve the read error after cleanup.
  return okay && stopped;
}



bool findLightSensor() {
  const uint8_t addresses[]={0x29,0x39,0x49};
  bool wrongDevice=false;
  for (size_t a=0;a<sizeof(addresses);++a) {
    uint8_t address=addresses[a];
    uint16_t id=0;
    if (!readLightRegister(address,0x8A,&id,1)) continue;
    const uint8_t part=id & 0xF0;
    if (part!=0x10 && part!=0x50) {
      wrongDevice=true;
      continue;
    }
    lightAddress=address;
    lightPart=part;
    lightFound=true;
    lightI2cError=0;
    return true;
  }
  lightStatus=wrongDevice ? "WRONG_DEVICE" : "NOT_FOUND";
  if (wrongDevice) lightI2cError=0;
  return false;
}

// Datasheet lux equations, normalized to 402 ms and 16x gain.
float convertLux(uint16_t broadband,uint16_t infrared,bool gain16) {
  if (broadband==0) return 0;
  const float scale=(322.0f/81.0f)*(gain16 ? 1.0f : 16.0f);
  const float ch0=broadband*scale, ch1=infrared*scale;
  const float ratio=ch1/ch0;
  float lux=0;
  if (lightPart==0x10) { // CS package.
    if (ratio<=0.52f) lux=0.0315f*ch0-0.0593f*ch0*powf(ratio,1.4f);
    else if (ratio<=0.65f) lux=0.0229f*ch0-0.0291f*ch1;
    else if (ratio<=0.80f) lux=0.0157f*ch0-0.0180f*ch1;
    else if (ratio<=1.30f) lux=0.00338f*ch0-0.00260f*ch1;
  } else { // T/FN/CL package, including the Grove module.
    if (ratio<=0.50f) lux=0.0304f*ch0-0.062f*ch0*powf(ratio,1.4f);
    else if (ratio<=0.61f) lux=0.0224f*ch0-0.031f*ch1;
    else if (ratio<=0.80f) lux=0.0128f*ch0-0.0153f*ch1;
    else if (ratio<=1.30f) lux=0.00146f*ch0-0.00112f*ch1;
  }
  return lux>0 ? lux : 0;
}

bool measureLight(uint32_t *lux) {
  *lux=0;
  if (!lightFound && !findLightSensor()) return false;
  uint16_t broadband=0, infrared=0;
  bool gain16=false;
  bool okay=readLightFrame(gain16,&broadband,&infrared);
  if (okay && broadband<200) {
    gain16=true;
    okay=readLightFrame(gain16,&broadband,&infrared);
  }
  if (!okay) {
    lightFound=false;
    lightStatus="I2C_READ_ERROR";
    return false;
  }
  if (broadband>37000 || infrared>37000) {
    lightStatus="SATURATED";
    return false;
  }
  *lux=(uint32_t)(convertLux(broadband,infrared,gain16)+0.5f);
  lightStatus="OK";
  return true;
}


void sensors_read(sensor_sample_t *s) {
    memset(s,0,sizeof(*s));
    s->distance_valid=measureDistance(&s->distance_cm,&s->echo_us,&s->distance_status);
    s->aht_valid=measureAht(&s->temperature_c,&s->humidity_pct);
    s->light_valid=measureLight(&s->light_lux);
    s->aht_status=ahtStatus; s->light_status=lightStatus;
    s->aht_i2c_error=ahtI2cError; s->light_i2c_error=lightI2cError;
    s->timestamp_ms=platform_millis();
}
