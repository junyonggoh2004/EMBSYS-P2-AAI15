#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bus/bus_if.h"
#include "bus/bus_common.h"
#include "hal/hal.h"
static uint8_t rx[300];
static size_t pos, end;
static uint64_t now;
static unsigned writes, delays;
int hal_uart_open(int tx,int r,uint32_t baud,uint8_t bits,char parity,uint8_t stop)
{ (void)tx;(void)r;assert(baud==256000 && bits==8 && parity=='N' && stop==1);return 0; }
int hal_uart_read_byte(int port) { assert(port==0); return pos<end?rx[pos++]:HAL_ERR_TIMEOUT; }
int hal_uart_write(int port,const uint8_t *p,size_t n) { (void)port;(void)p;++writes;return (int)n; }
uint32_t hal_uart_rx_dropped(int port) { assert(port==0);return 7; }
uint64_t hal_time_us(void) { return now; }
void hal_delay_us(uint32_t us) { ++delays;now+=us; }
void hal_delay_ms(uint32_t ms) { ++delays;now+=(uint64_t)ms*1000; }
int main(void)
{
    app_cfg_t config={0};
    config.uart_baud=256000;config.uart_bits=8;config.uart_parity='N';config.uart_stop=1;
    config.uart_pre[0]=1;config.uart_pre_len=1;config.uart_post_delay_ms=5;
    sensor_cfg_t sensor={"RADAR","uart",&config};
    void *context;
    assert(bus_uart.init_cfg(&sensor,&context)==0);
    for(unsigned i=0;i<300;++i) rx[i]=(uint8_t)i;
    end=300;
    unsigned before=delays, before_writes=writes;
    uint64_t before_time=now;
    uint8_t bytes[128];
    assert(bus_uart.read_ready(context,bytes,sizeof bytes)==128 && bytes[127]==127);
    assert(bus_uart.read_ready(context,bytes,sizeof bytes)==128 && bytes[0]==128);
    assert(bus_uart.read_ready(context,bytes,sizeof bytes)==44 && bytes[0]==0);
    assert(bus_uart.read_ready(context,bytes,sizeof bytes)==0);
    assert(now==before_time && delays==before && writes==before_writes);
    assert(bus_uart.rx_dropped(context)==7);
    bus_uart.deinit(context);
    puts("UART ready drain: PASS (real driver, bounded reads, no wait/pre-writes, loss reporting)");
}
