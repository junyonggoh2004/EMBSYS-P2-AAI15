#ifndef LCD_INTERNAL_H
#define LCD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lcd_output.h"

int lcd_transport_open(void);
void lcd_transport_close(void);
int lcd_transport_reset(void);
int lcd_transport_command(uint8_t command, const uint8_t *data, size_t length);
int lcd_transport_data(const uint8_t *data, size_t length);

int lcd_st7789_init(const lcd_output_config_t *config);
int lcd_st7789_fill_rect(const lcd_output_config_t *config, uint16_t x,
                          uint16_t y, uint16_t width, uint16_t height,
                          uint16_t color);
int lcd_st7789_blit(const lcd_output_config_t *config, uint16_t x, uint16_t y,
                     uint16_t width, uint16_t height, const uint16_t *pixels);

/* Bit 0 is the top pixel of a 5x7 character column. */
uint8_t lcd_font_5x7_column(char character, uint8_t column);

#endif
