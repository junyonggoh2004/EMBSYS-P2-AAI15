/* Polled GPIO + open-drain I2C. No Arduino or FreeRTOS driver calls. */
#include "platform.h"
#include "app_config.h"
#include <tk/tkernel.h>
#include "esp_cpu.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"
#include "sdkconfig.h"

void platform_gpio_write(int pin,bool high) { gpio_ll_set_level(GPIO_LL_GET_HW(0),pin,high); }
bool platform_gpio_read(int pin) { return gpio_ll_get_level(GPIO_LL_GET_HW(0),pin)!=0; }
uint32_t platform_cycles(void) { return esp_cpu_get_cycle_count(); }
uint32_t platform_elapsed_us(uint32_t start) {
    return (uint32_t)(platform_cycles()-start)/CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
}
void platform_delay_us(uint32_t us) { esp_rom_delay_us(us); }
void platform_sleep_ms(uint32_t ms) { tk_dly_tsk(ms); }
uint32_t platform_millis(void) {
    SYSTIM time; tk_get_otm(&time); return time.lo;
}
static void pin_setup(int pin,bool output,bool open_drain,bool pullup) {
    gpio_dev_t *gpio=GPIO_LL_GET_HW(0);
    gpio_ll_func_sel(gpio,pin,PIN_FUNC_GPIO);
    esp_rom_gpio_connect_out_signal(pin,SIG_GPIO_OUT_IDX,false,false);
    gpio_ll_input_enable(gpio,pin);
    gpio_ll_pullup_dis(gpio,pin); gpio_ll_pulldown_dis(gpio,pin);
    if (pullup) gpio_ll_pullup_en(gpio,pin);
    if (open_drain) gpio_ll_od_enable(gpio,pin); else gpio_ll_od_disable(gpio,pin);
    if (output) gpio_ll_output_enable(gpio,pin); else gpio_ll_output_disable(gpio,pin);
}
static void half(void) { platform_delay_us(5); }
static bool clock_high(void) {
    platform_gpio_write(SENSOR_SCL,true);
    uint32_t start=platform_cycles();
    while (!platform_gpio_read(SENSOR_SCL)) {
        if (platform_elapsed_us(start)>=2000) return false;
    }
    half(); return true;
}
static bool stop(void) {
    platform_gpio_write(SENSOR_SCL,false);
    platform_gpio_write(SENSOR_SDA,false); half();
    bool okay=clock_high();
    platform_gpio_write(SENSOR_SDA,true); half();
    return okay;
}
static int start(bool first) {
    platform_gpio_write(SENSOR_SDA,true); half();
    if (!clock_high()) return 5;
    if (first && !platform_gpio_read(SENSOR_SDA)) return 6;
    platform_gpio_write(SENSOR_SDA,false); half();
    platform_gpio_write(SENSOR_SCL,false);
    return 0;
}
static int write_byte(uint8_t value) {
    for (int bit=7;bit>=0;--bit) {
        platform_gpio_write(SENSOR_SDA,(value>>bit)&1); half();
        if (!clock_high()) return 5;
        platform_gpio_write(SENSOR_SCL,false);
    }
    platform_gpio_write(SENSOR_SDA,true); half();
    if (!clock_high()) return 5;
    bool nack=platform_gpio_read(SENSOR_SDA);
    platform_gpio_write(SENSOR_SCL,false);
    return nack ? 3 : 0;
}
static int read_byte(uint8_t *value,bool last) {
    *value=0; platform_gpio_write(SENSOR_SDA,true);
    for (int bit=7;bit>=0;--bit) {
        half(); if (!clock_high()) return 5;
        if (platform_gpio_read(SENSOR_SDA)) *value|=(1u<<bit);
        platform_gpio_write(SENSOR_SCL,false);
    }
    platform_gpio_write(SENSOR_SDA,last); half();
    if (!clock_high()) return 5;
    platform_gpio_write(SENSOR_SCL,false);
    platform_gpio_write(SENSOR_SDA,true);
    return 0;
}
int platform_i2c(uint8_t address,const uint8_t *tx,size_t nt,uint8_t *rx,size_t nr) {
    int rc=start(true);
    if (rc) goto done;
    if (nt || !nr) {
        rc=write_byte(address<<1); if (rc==3) rc=2;
        for (size_t i=0;!rc && i<nt;++i) rc=write_byte(tx[i]);
    }
    if (!rc && nr) {
        if (nt) rc=start(false);
        if (!rc) { rc=write_byte((address<<1)|1); if (rc==3) rc=2; }
        for (size_t i=0;!rc && i<nr;++i) rc=read_byte(&rx[i],i+1==nr);
    }
done:
    if (!stop() && !rc) rc=5;
    return rc;
}
void platform_init(void) {
    platform_gpio_write(ULTRASONIC_TRIG,false);
    pin_setup(ULTRASONIC_TRIG,true,false,false);
    pin_setup(ULTRASONIC_ECHO,false,false,false);
    gpio_ll_pulldown_en(GPIO_LL_GET_HW(0),ULTRASONIC_ECHO);
    platform_gpio_write(SENSOR_SDA,true); platform_gpio_write(SENSOR_SCL,true);
    pin_setup(SENSOR_SDA,true,true,true); pin_setup(SENSOR_SCL,true,true,true);
    /* Release an interrupted I2C transaction, bounded to nine clocks. */
    for (int i=0;i<9 && !platform_gpio_read(SENSOR_SDA);++i) {
        platform_gpio_write(SENSOR_SCL,false); half(); if (!clock_high()) break;
    }
    stop();
}
