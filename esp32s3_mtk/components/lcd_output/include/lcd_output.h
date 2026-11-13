#ifndef LCD_OUTPUT_H
#define LCD_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dimensions and RAM offsets depend on the actual ST7789 breakout. Supply
 * verified values at bring-up; there is deliberately no guessed default. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
    uint8_t madctl;
    bool invert_colors;
} lcd_output_config_t;

/* Single caller only, from a task (never an ISR). All drawing calls block for
 * bounded SPI transfers. Returns 0 on success or a negative HAL_ERR_* code. */
int lcd_output_init(const lcd_output_config_t *config);
void lcd_output_deinit(void);
bool lcd_output_ready(void);

/* RGB565 colours. Coordinates are in the configured visible area. */
int lcd_output_fill(uint16_t color);
int lcd_output_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height, uint16_t color);
int lcd_output_draw_text(uint16_t x, uint16_t y, const char *text,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale);

/* Basic physical bring-up check: red, green, blue bands and a label. */
int lcd_output_test_pattern(void);

/* Portrait demo layout for five console-supplied mock values. The caller
 * initializes the verified 76x284 panel first; no sensor is read here. */
int lcd_output_mock_screen(const char *distance_cm, const char *temp_c,
                           const char *humidity_pct, const char *light_lux,
                           const char *ml_output);

#ifdef __cplusplus
}
#endif

#endif
