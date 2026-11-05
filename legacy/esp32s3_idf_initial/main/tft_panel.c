#include "tft_panel.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_SWRESET 0x01
#define LCD_SLPOUT  0x11
#define LCD_NORON   0x13
#define LCD_INVOFF  0x20
#define LCD_INVON   0x21
#define LCD_DISPON  0x29
#define LCD_CASET   0x2A
#define LCD_RASET   0x2B
#define LCD_RAMWR   0x2C
#define LCD_MADCTL  0x36
#define LCD_COLMOD  0x3A

typedef struct {
    bool initialized;
    spi_device_handle_t spi;
    gpio_num_t dc;
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
} tft_context_t;

static tft_context_t s_tft;

static esp_err_t transmit(bool data_mode, const void *bytes, size_t length)
{
    if (length == 0) return ESP_OK;
    if (gpio_set_level(s_tft.dc, data_mode ? 1 : 0) != ESP_OK) return ESP_FAIL;
    spi_transaction_t transaction = {
        .length = length * 8,
        .tx_buffer = bytes,
    };
    return spi_device_polling_transmit(s_tft.spi, &transaction);
}

static esp_err_t command(uint8_t cmd, const uint8_t *data, size_t length)
{
    esp_err_t error = transmit(false, &cmd, 1);
    if (error == ESP_OK && data != NULL && length != 0) {
        error = transmit(true, data, length);
    }
    return error;
}

static uint8_t madctl_value(tft_rotation_t rotation, bool bgr)
{
    /* MY, MX and MV are D7, D6 and D5. BGR is D3. */
    static const uint8_t rotations[] = {0x00, 0x60, 0xC0, 0xA0};
    return rotations[rotation] | (bgr ? 0x08 : 0x00);
}

static esp_err_t init_st7789p3(void)
{
    const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    const uint8_t gate = 0x35;
    const uint8_t vcom = 0x19;
    const uint8_t lcm = 0x2C;
    const uint8_t vdv_enable[] = {0x01, 0xFF};
    const uint8_t vrh = 0x12;
    const uint8_t vdv = 0x20;
    const uint8_t frame = 0x0F;
    const uint8_t power[] = {0xA4, 0xA1};
    if (command(0xB2, porch, sizeof(porch)) != ESP_OK ||
        command(0xB7, &gate, 1) != ESP_OK ||
        command(0xBB, &vcom, 1) != ESP_OK ||
        command(0xC0, &lcm, 1) != ESP_OK ||
        command(0xC2, vdv_enable, sizeof(vdv_enable)) != ESP_OK ||
        command(0xC3, &vrh, 1) != ESP_OK ||
        command(0xC4, &vdv, 1) != ESP_OK ||
        command(0xC6, &frame, 1) != ESP_OK ||
        command(0xD0, power, sizeof(power)) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t init_nv3023b(void)
{
    /* NV3023B supports the same DCS display-on, address-window, memory-write,
       MADCTL and COLMOD commands used below. Keep module-specific power/gamma
       tuning out of the generic driver; it can be added here for a known panel. */
    return command(LCD_NORON, NULL, 0);
}

app_status_t tft_panel_init(const hardware_config_t *hardware,
                            const device_config_t *device)
{
    if (hardware == NULL || device == NULL || device->tft.width == 0 ||
        device->tft.height == 0 || device->tft.rotation > TFT_ROTATION_270 ||
        device->tft.controller > TFT_CONTROLLER_NV3023B) {
        return APP_STATUS_INVALID_ARGUMENT;
    }
    memset(&s_tft, 0, sizeof(s_tft));

    gpio_config_t controls = {
        .pin_bit_mask = (1ULL << hardware->tft.dc) |
                        (1ULL << hardware->tft.reset),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (device->tft.backlight_is_logic_input) {
        controls.pin_bit_mask |= 1ULL << hardware->tft.backlight;
    }
    if (gpio_config(&controls) != ESP_OK) return APP_STATUS_SPI_ERROR;
    s_tft.dc = hardware->tft.dc;

    spi_bus_config_t bus = {
        .mosi_io_num = hardware->tft.mosi,
        .miso_io_num = hardware->tft.miso,
        .sclk_io_num = hardware->tft.sclk,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = device->tft.width * 2,
    };
    if (spi_bus_initialize(hardware->tft.host, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return APP_STATUS_SPI_ERROR;
    }
    spi_device_interface_config_t spi = {
        .clock_speed_hz = hardware->tft.frequency_hz,
        .mode = hardware->tft.mode,
        .spics_io_num = hardware->tft.cs,
        .queue_size = 1,
    };
    if (spi_bus_add_device(hardware->tft.host, &spi, &s_tft.spi) != ESP_OK) {
        spi_bus_free(hardware->tft.host);
        return APP_STATUS_SPI_ERROR;
    }

    gpio_set_level(hardware->tft.reset, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(hardware->tft.reset, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    if (command(LCD_SWRESET, NULL, 0) != ESP_OK) return APP_STATUS_SPI_ERROR;
    vTaskDelay(pdMS_TO_TICKS(150));
    if (command(LCD_SLPOUT, NULL, 0) != ESP_OK) return APP_STATUS_SPI_ERROR;
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_err_t error = device->tft.controller == TFT_CONTROLLER_ST7789P3
                          ? init_st7789p3() : init_nv3023b();
    const uint8_t pixel_format = 0x55; /* 16-bit RGB565 */
    const uint8_t madctl = madctl_value(device->tft.rotation,
                                        device->tft.bgr_order);
    if (error != ESP_OK || command(LCD_COLMOD, &pixel_format, 1) != ESP_OK ||
        command(LCD_MADCTL, &madctl, 1) != ESP_OK ||
        command(device->tft.inverted ? LCD_INVON : LCD_INVOFF, NULL, 0) != ESP_OK ||
        command(LCD_NORON, NULL, 0) != ESP_OK ||
        command(LCD_DISPON, NULL, 0) != ESP_OK) {
        return APP_STATUS_SPI_ERROR;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    if (device->tft.backlight_is_logic_input) {
        gpio_set_level(hardware->tft.backlight,
                       device->tft.backlight_active_high ? 1 : 0);
    }
    s_tft.width = device->tft.width;
    s_tft.height = device->tft.height;
    s_tft.x_offset = device->tft.x_offset;
    s_tft.y_offset = device->tft.y_offset;
    s_tft.initialized = true;
    return APP_STATUS_OK;
}

static esp_err_t set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t x0 = x + s_tft.x_offset;
    const uint16_t y0 = y + s_tft.y_offset;
    const uint16_t x1 = x0 + width - 1;
    const uint16_t y1 = y0 + height - 1;
    const uint8_t columns[] = {x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff};
    const uint8_t rows[] = {y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff};
    if (command(LCD_CASET, columns, sizeof(columns)) != ESP_OK ||
        command(LCD_RASET, rows, sizeof(rows)) != ESP_OK ||
        command(LCD_RAMWR, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

tft_result_t tft_panel_draw(uint16_t x, uint16_t y, uint16_t width,
                            uint16_t height, const uint16_t *pixels)
{
    tft_result_t result = {.status = APP_STATUS_NOT_INITIALIZED};
    if (!s_tft.initialized) return result;
    if (pixels == NULL || width == 0 || height == 0 ||
        x + width > s_tft.width || y + height > s_tft.height) {
        result.status = APP_STATUS_INVALID_ARGUMENT;
        return result;
    }
    if (set_window(x, y, width, height) != ESP_OK) {
        result.status = APP_STATUS_SPI_ERROR;
        return result;
    }

    uint8_t *line = heap_caps_malloc(width * 2, MALLOC_CAP_DMA);
    if (line == NULL) {
        result.status = APP_STATUS_NO_MEMORY;
        return result;
    }
    for (uint16_t row = 0; row < height; ++row) {
        for (uint16_t column = 0; column < width; ++column) {
            const uint16_t pixel = pixels[(size_t)row * width + column];
            line[column * 2] = pixel >> 8;
            line[column * 2 + 1] = pixel & 0xff;
        }
        if (transmit(true, line, width * 2) != ESP_OK) {
            heap_caps_free(line);
            result.status = APP_STATUS_SPI_ERROR;
            return result;
        }
        result.pixels_written += width;
    }
    heap_caps_free(line);
    result.status = APP_STATUS_OK;
    return result;
}

tft_result_t tft_panel_fill(uint16_t rgb565)
{
    tft_result_t result = {.status = APP_STATUS_NOT_INITIALIZED};
    if (!s_tft.initialized) return result;
    if (set_window(0, 0, s_tft.width, s_tft.height) != ESP_OK) {
        result.status = APP_STATUS_SPI_ERROR;
        return result;
    }
    uint8_t *line = heap_caps_malloc(s_tft.width * 2, MALLOC_CAP_DMA);
    if (line == NULL) {
        result.status = APP_STATUS_NO_MEMORY;
        return result;
    }
    for (uint16_t column = 0; column < s_tft.width; ++column) {
        line[column * 2] = rgb565 >> 8;
        line[column * 2 + 1] = rgb565 & 0xff;
    }
    for (uint16_t row = 0; row < s_tft.height; ++row) {
        if (transmit(true, line, s_tft.width * 2) != ESP_OK) {
            heap_caps_free(line);
            result.status = APP_STATUS_SPI_ERROR;
            return result;
        }
        result.pixels_written += s_tft.width;
    }
    heap_caps_free(line);
    result.status = APP_STATUS_OK;
    return result;
}
