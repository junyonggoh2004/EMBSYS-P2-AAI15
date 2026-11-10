#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void platform_init(void);
uint32_t platform_millis(void);
void platform_sleep_ms(uint32_t ms);
void platform_delay_us(uint32_t us);
void platform_gpio_write(int pin,bool high);
bool platform_gpio_read(int pin);
uint32_t platform_cycles(void);
uint32_t platform_elapsed_us(uint32_t start);
/* 0 success, 2 address NACK, 3 data NACK, 5 clock timeout, 6 stuck bus. */
int platform_i2c(uint8_t address,const uint8_t *tx,size_t nt,uint8_t *rx,size_t nr);
