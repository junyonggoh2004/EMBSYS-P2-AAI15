#ifndef LCD_BOARD_CONFIG_H
#define LCD_BOARD_CONFIG_H

/* Verified LCD-only ESP32-S3 Super Mini wiring for the DIYUSER ST7789P3.
 * The Version 2 shared-breadboard connections remain in WIRING.md.
 * LCD SCL/SDA are SPI SCK/MOSI, not the sensor I2C bus.
 * Verified test: LCDTEST 76 284 82 18 00 0. Geometry, offsets and
 * inversion remain runtime arguments rather than fixed values here. */
#define LCD_PIN_SCK   12
#define LCD_PIN_MOSI  11
#define LCD_PIN_DC     9
#define LCD_PIN_RST    8
#define LCD_PIN_CS    10
#define LCD_PIN_MISO  -1

/* Conservative bring-up settings; increase SPI speed only after board tests. */
#define LCD_SPI_HZ          1000000u
#define LCD_SPI_MODE        0u
#define LCD_SPI_TIMEOUT_MS  100u
#define LCD_SPI_CHUNK_BYTES 256u

/* BL is not GPIO-driven: its electrical load has not been verified. */

#endif
