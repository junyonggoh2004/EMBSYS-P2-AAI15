#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hal/hal.h"
#include "lcd_board_config.h"
#include "lcd_output.h"

typedef struct { uint8_t dc; uint8_t byte; } wire_byte_t;
static wire_byte_t wire[4096];
static size_t wire_count;
static int dc_level;
static int spi_opens;
static int spi_closes;
static int fail_next_transfer;

bool hal_gpio_valid(int pin) { return pin >= 0 && pin < 48; }
int hal_gpio_mode(int pin, hal_gpio_mode_t mode)
{
    assert(hal_gpio_valid(pin));
    assert(mode == HAL_GPIO_OUTPUT);
    return HAL_OK;
}
int hal_gpio_write(int pin, int level)
{
    if (pin == LCD_PIN_DC) dc_level = level;
    return HAL_OK;
}
void hal_delay_ms(uint32_t ms) { (void)ms; }
int hal_spi_open(const char *host, int sclk, int mosi, int miso, int cs,
                 uint32_t hz, uint8_t mode)
{
    assert(strcmp(host, "fspi") == 0);
    assert(sclk == LCD_PIN_SCK && mosi == LCD_PIN_MOSI &&
           miso == LCD_PIN_MISO && cs == LCD_PIN_CS);
    assert(hz == 1000000u && mode == 0u);
    ++spi_opens;
    return 0;
}
int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx,
                     size_t len, uint32_t timeout_ms)
{
    assert(device == 0 && tx && !rx && len && timeout_ms);
    if (fail_next_transfer) {
        fail_next_transfer = 0;
        return HAL_ERR_IO;
    }
    assert(wire_count + len <= sizeof wire / sizeof wire[0]);
    for (size_t i = 0; i < len; ++i)
        wire[wire_count++] = (wire_byte_t){ (uint8_t)dc_level, tx[i] };
    return (int)len;
}
int hal_spi_close(int device)
{
    assert(device == 0);
    ++spi_closes;
    return HAL_OK;
}

static size_t find_command(uint8_t command)
{
    for (size_t i = 0; i < wire_count; ++i)
        if (wire[i].dc == 0 && wire[i].byte == command) return i;
    return wire_count;
}

int main(void)
{
    lcd_output_config_t invalid = { 0 };
    assert(lcd_output_init(&invalid) == HAL_ERR_INVALID);
    assert(spi_opens == 0);

    lcd_output_config_t config = {
        .width = 76, .height = 284, .x_offset = 82, .y_offset = 18,
        .madctl = 0, .invert_colors = true
    };
    assert(lcd_output_init(&config) == HAL_OK);
    assert(lcd_output_ready());
    assert(spi_opens == 1);
    assert(find_command(0x01) < find_command(0x11));
    assert(find_command(0x11) < find_command(0x3a));
    size_t color_mode = find_command(0x3a);
    assert(color_mode + 1 < wire_count);
    assert(wire[color_mode + 1].dc == 1 && wire[color_mode + 1].byte == 0x55);
    assert(find_command(0x21) < wire_count);
    assert(find_command(0x29) < wire_count);

    wire_count = 0;
    assert(lcd_output_fill_rect(1, 2, 2, 2, 0xf81f) == HAL_OK);
    const wire_byte_t expected[] = {
        {0, 0x2a}, {1, 0x00}, {1, 0x53}, {1, 0x00}, {1, 0x54},
        {0, 0x2b}, {1, 0x00}, {1, 0x14}, {1, 0x00}, {1, 0x15},
        {0, 0x2c}, {1, 0xf8}, {1, 0x1f}, {1, 0xf8}, {1, 0x1f},
        {1, 0xf8}, {1, 0x1f}, {1, 0xf8}, {1, 0x1f}
    };
    assert(wire_count == sizeof expected / sizeof expected[0]);
    assert(memcmp(wire, expected, sizeof expected) == 0);

    wire_count = 0;
    assert(lcd_output_fill_rect(75, 0, 2, 1, 0) == HAL_ERR_INVALID);
    assert(wire_count == 0);
    assert(lcd_output_draw_text(0, 0, "LCD 3", 0xffff, 0, 2) == HAL_OK);
    assert(find_command(0x2c) < wire_count);

    fail_next_transfer = 1;
    assert(lcd_output_fill_rect(0, 0, 1, 1, 0) == HAL_ERR_IO);
    lcd_output_deinit();
    assert(!lcd_output_ready() && spi_closes == 1);
    assert(lcd_output_fill(0) == HAL_ERR_INVALID);
    puts("lcd output host test: PASS");
    return 0;
}
