#include <stddef.h>

#include "lcd_output.h"
#include "hal/hal.h"

typedef struct {
    const char *label;
    const char *value;
    uint16_t accent;
} mock_card_t;

int lcd_output_mock_screen(const char *distance_cm, const char *temp_c,
                           const char *humidity_pct, const char *light_lux,
                           const char *ml_output)
{
    if (!distance_cm || !temp_c || !humidity_pct || !light_lux || !ml_output)
        return HAL_ERR_INVALID;

    const mock_card_t cards[5] = {
        { "DISTANCE CM", distance_cm, 0x05ffu },
        { "TEMP C", temp_c, 0xf96au },
        { "HUMIDITY", humidity_pct, 0x07e0u },
        { "LIGHT LUX", light_lux, 0xff20u },
        { "ML OUTPUT", ml_output, 0xc81fu },
    };
    int result = lcd_output_fill(0x0000u);
    if (result != HAL_OK) return result;
    result = lcd_output_draw_text(2, 3, "ROOM MONITOR", 0xffffu, 0x0000u, 1);
    if (result != HAL_OK) return result;
    result = lcd_output_fill_rect(2, 17, 72, 2, 0x05ffu);
    if (result != HAL_OK) return result;

    for (size_t i = 0; i < 5; ++i) {
        uint16_t y = (uint16_t)(24u + i * 48u);
        result = lcd_output_fill_rect(1, y, 74, 44, 0x10a2u);
        if (result != HAL_OK) return result;
        result = lcd_output_fill_rect(1, y, 4, 44, cards[i].accent);
        if (result != HAL_OK) return result;
        result = lcd_output_draw_text(8, (uint16_t)(y + 3u), cards[i].label,
                                      0xffffu, 0x10a2u, 1);
        if (result != HAL_OK) return result;
        result = lcd_output_draw_text(8, (uint16_t)(y + 18u), cards[i].value,
                                      cards[i].accent, 0x10a2u, 2);
        if (result != HAL_OK) return result;
    }
    return lcd_output_draw_text(2, 270, "3/3 OK", 0x07e0u, 0x0000u, 1);
}
