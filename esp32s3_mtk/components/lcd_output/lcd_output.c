#include "lcd_internal.h"
#include "hal/hal.h"

static lcd_output_config_t s_config;
static bool s_ready;

int lcd_output_init(const lcd_output_config_t *config)
{
    if (s_ready) return HAL_ERR_BUSY;
    if (!config || !config->width || !config->height ||
        config->width > 320u || config->height > 320u ||
        (uint32_t)config->x_offset + config->width > 320u ||
        (uint32_t)config->y_offset + config->height > 320u)
        return HAL_ERR_INVALID;

    int result = lcd_transport_open();
    if (result != HAL_OK) return result;
    result = lcd_st7789_init(config);
    if (result != HAL_OK) {
        lcd_transport_close();
        return result;
    }
    s_config = *config;
    s_ready = true;
    return HAL_OK;
}

void lcd_output_deinit(void)
{
    s_ready = false;
    lcd_transport_close();
}

bool lcd_output_ready(void)
{
    return s_ready;
}

int lcd_output_fill(uint16_t color)
{
    if (!s_ready) return HAL_ERR_INVALID;
    return lcd_st7789_fill_rect(&s_config, 0, 0, s_config.width,
                                 s_config.height, color);
}

int lcd_output_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                         uint16_t height, uint16_t color)
{
    if (!s_ready) return HAL_ERR_INVALID;
    return lcd_st7789_fill_rect(&s_config, x, y, width, height, color);
}

int lcd_output_draw_text(uint16_t x, uint16_t y, const char *text,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale)
{
    if (!s_ready || !text || scale < 1u || scale > 3u) return HAL_ERR_INVALID;
    const uint16_t cell_width = (uint16_t)(6u * scale);
    const uint16_t cell_height = (uint16_t)(8u * scale);
    if (y >= s_config.height || cell_height > s_config.height - y)
        return HAL_ERR_INVALID;

    /* Drawing is single-caller; keep the largest glyph off the task stack. */
    static uint16_t cell[18u * 24u];
    uint32_t cursor = x;
    for (const char *p = text; *p; ++p) {
        if (cursor >= s_config.width || cell_width > s_config.width - cursor)
            break;
        for (uint16_t py = 0; py < cell_height; ++py) {
            uint8_t row = (uint8_t)(py / scale);
            for (uint16_t px = 0; px < cell_width; ++px) {
                uint8_t column = (uint8_t)(px / scale);
                bool on = column < 5u && row < 7u &&
                    (lcd_font_5x7_column(*p, column) & (1u << row));
                cell[(size_t)py * cell_width + px] = on ? foreground : background;
            }
        }
        int result = lcd_st7789_blit(&s_config, (uint16_t)cursor, y,
                                      cell_width, cell_height, cell);
        if (result != HAL_OK) return result;
        cursor += cell_width;
    }
    return HAL_OK;
}

int lcd_output_test_pattern(void)
{
    if (!s_ready || s_config.width < 54u || s_config.height < 40u)
        return HAL_ERR_INVALID;
    const uint16_t band = (uint16_t)(s_config.height / 4u);
    int result = lcd_output_fill_rect(0, 0, s_config.width, band, 0xf800u);
    if (result != HAL_OK) return result;
    result = lcd_output_fill_rect(0, band, s_config.width, band, 0x07e0u);
    if (result != HAL_OK) return result;
    result = lcd_output_fill_rect(0, (uint16_t)(band * 2u),
                                   s_config.width, band, 0x001fu);
    if (result != HAL_OK) return result;
    result = lcd_output_fill_rect(0, (uint16_t)(band * 3u),
                                   s_config.width,
                                   (uint16_t)(s_config.height - band * 3u),
                                   0x0000u);
    if (result != HAL_OK) return result;
    return lcd_output_draw_text(3, (uint16_t)(band * 3u + 2u),
                                 "LCD READY", 0xffffu, 0x0000u, 1u);
}
