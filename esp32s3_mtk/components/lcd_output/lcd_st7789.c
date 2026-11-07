#include "lcd_internal.h"
#include "hal/hal.h"

#define ST_SWRESET 0x01u
#define ST_SLPOUT  0x11u
#define ST_NORON   0x13u
#define ST_INVOFF  0x20u
#define ST_INVON   0x21u
#define ST_DISPON  0x29u
#define ST_CASET   0x2au
#define ST_RASET   0x2bu
#define ST_RAMWR   0x2cu
#define ST_MADCTL  0x36u
#define ST_COLMOD  0x3au

#define PIXELS_PER_CHUNK 128u

static int set_window(const lcd_output_config_t *config, uint16_t x,
                      uint16_t y, uint16_t width, uint16_t height)
{
    if (!config || !width || !height || x >= config->width ||
        y >= config->height || width > config->width - x ||
        height > config->height - y)
        return HAL_ERR_INVALID;

    uint16_t x0 = (uint16_t)(x + config->x_offset);
    uint16_t x1 = (uint16_t)(x0 + width - 1u);
    uint16_t y0 = (uint16_t)(y + config->y_offset);
    uint16_t y1 = (uint16_t)(y0 + height - 1u);
    uint8_t columns[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0,
                            (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t rows[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0,
                         (uint8_t)(y1 >> 8), (uint8_t)y1 };
    int result = lcd_transport_command(ST_CASET, columns, sizeof columns);
    if (result != HAL_OK) return result;
    return lcd_transport_command(ST_RASET, rows, sizeof rows);
}

int lcd_st7789_init(const lcd_output_config_t *config)
{
    if (!config || !config->width || !config->height ||
        config->width > 320u || config->height > 320u ||
        (uint32_t)config->x_offset + config->width > 320u ||
        (uint32_t)config->y_offset + config->height > 320u)
        return HAL_ERR_INVALID;

    int result = lcd_transport_reset();
    if (result != HAL_OK) return result;
    result = lcd_transport_command(ST_SWRESET, NULL, 0);
    if (result != HAL_OK) return result;
    hal_delay_ms(150);
    result = lcd_transport_command(ST_SLPOUT, NULL, 0);
    if (result != HAL_OK) return result;
    hal_delay_ms(130);
    const uint8_t rgb565 = 0x55u;
    result = lcd_transport_command(ST_COLMOD, &rgb565, 1);
    if (result != HAL_OK) return result;
    result = lcd_transport_command(ST_MADCTL, &config->madctl, 1);
    if (result != HAL_OK) return result;
    result = lcd_transport_command(config->invert_colors ? ST_INVON : ST_INVOFF,
                                    NULL, 0);
    if (result != HAL_OK) return result;
    result = lcd_transport_command(ST_NORON, NULL, 0);
    if (result != HAL_OK) return result;
    hal_delay_ms(10);
    result = lcd_transport_command(ST_DISPON, NULL, 0);
    if (result != HAL_OK) return result;
    hal_delay_ms(10);
    return HAL_OK;
}

int lcd_st7789_fill_rect(const lcd_output_config_t *config, uint16_t x,
                          uint16_t y, uint16_t width, uint16_t height,
                          uint16_t color)
{
    int result = set_window(config, x, y, width, height);
    if (result != HAL_OK) return result;
    result = lcd_transport_command(ST_RAMWR, NULL, 0);
    if (result != HAL_OK) return result;

    uint8_t bytes[PIXELS_PER_CHUNK * 2u];
    for (size_t i = 0; i < sizeof bytes; i += 2u) {
        bytes[i] = (uint8_t)(color >> 8);
        bytes[i + 1u] = (uint8_t)color;
    }
    uint32_t remaining = (uint32_t)width * height;
    while (remaining) {
        uint32_t count = remaining < PIXELS_PER_CHUNK ? remaining : PIXELS_PER_CHUNK;
        result = lcd_transport_data(bytes, count * 2u);
        if (result != HAL_OK) return result;
        remaining -= count;
    }
    return HAL_OK;
}

int lcd_st7789_blit(const lcd_output_config_t *config, uint16_t x, uint16_t y,
                     uint16_t width, uint16_t height, const uint16_t *pixels)
{
    if (!pixels) return HAL_ERR_INVALID;
    int result = set_window(config, x, y, width, height);
    if (result != HAL_OK) return result;
    result = lcd_transport_command(ST_RAMWR, NULL, 0);
    if (result != HAL_OK) return result;

    uint8_t bytes[PIXELS_PER_CHUNK * 2u];
    uint32_t remaining = (uint32_t)width * height;
    for (uint32_t offset = 0; remaining; ) {
        uint32_t count = remaining < PIXELS_PER_CHUNK ? remaining : PIXELS_PER_CHUNK;
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t pixel = pixels[offset + i];
            bytes[2u * i] = (uint8_t)(pixel >> 8);
            bytes[2u * i + 1u] = (uint8_t)pixel;
        }
        result = lcd_transport_data(bytes, count * 2u);
        if (result != HAL_OK) return result;
        offset += count;
        remaining -= count;
    }
    return HAL_OK;
}
