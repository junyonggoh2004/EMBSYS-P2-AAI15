#include "lcd_internal.h"
#include "lcd_board_config.h"
#include "hal/hal.h"

static int s_spi_device = -1;

int lcd_transport_open(void)
{
    if (s_spi_device >= 0) return HAL_ERR_BUSY;
    if (hal_gpio_mode(LCD_PIN_DC, HAL_GPIO_OUTPUT) != HAL_OK ||
        hal_gpio_mode(LCD_PIN_RST, HAL_GPIO_OUTPUT) != HAL_OK)
        return HAL_ERR_PIN;
    /* Leave reset deasserted if opening SPI fails. */
    hal_gpio_write(LCD_PIN_RST, 1);
    s_spi_device = hal_spi_open("fspi", LCD_PIN_SCK, LCD_PIN_MOSI,
                                LCD_PIN_MISO, LCD_PIN_CS, LCD_SPI_HZ,
                                LCD_SPI_MODE);
    return s_spi_device < 0 ? s_spi_device : HAL_OK;
}

void lcd_transport_close(void)
{
    if (s_spi_device >= 0) {
        hal_spi_close(s_spi_device);
        s_spi_device = -1;
    }
}

int lcd_transport_reset(void)
{
    if (s_spi_device < 0) return HAL_ERR_INVALID;
    if (hal_gpio_write(LCD_PIN_RST, 1) != HAL_OK) return HAL_ERR_PIN;
    hal_delay_ms(10);
    if (hal_gpio_write(LCD_PIN_RST, 0) != HAL_OK) return HAL_ERR_PIN;
    hal_delay_ms(10);
    if (hal_gpio_write(LCD_PIN_RST, 1) != HAL_OK) return HAL_ERR_PIN;
    hal_delay_ms(120);
    return HAL_OK;
}

int lcd_transport_data(const uint8_t *data, size_t length)
{
    if (s_spi_device < 0 || (!data && length)) return HAL_ERR_INVALID;
    if (!length) return HAL_OK;
    if (hal_gpio_write(LCD_PIN_DC, 1) != HAL_OK) return HAL_ERR_PIN;
    for (size_t offset = 0; offset < length; ) {
        size_t chunk = length - offset;
        if (chunk > LCD_SPI_CHUNK_BYTES) chunk = LCD_SPI_CHUNK_BYTES;
        int result = hal_spi_transfer(s_spi_device, data + offset, NULL,
                                      chunk, LCD_SPI_TIMEOUT_MS);
        if (result != (int)chunk) return result < 0 ? result : HAL_ERR_IO;
        offset += chunk;
    }
    return HAL_OK;
}

int lcd_transport_command(uint8_t command, const uint8_t *data, size_t length)
{
    if (s_spi_device < 0 || (!data && length)) return HAL_ERR_INVALID;
    if (hal_gpio_write(LCD_PIN_DC, 0) != HAL_OK) return HAL_ERR_PIN;
    int result = hal_spi_transfer(s_spi_device, &command, NULL, 1,
                                  LCD_SPI_TIMEOUT_MS);
    if (result != 1) return result < 0 ? result : HAL_ERR_IO;
    return lcd_transport_data(data, length);
}
