#ifndef LCD_FLASH_BOARD_H
#define LCD_FLASH_BOARD_H

/* LCD + external SPI flash breadboard in WIRING.md. Both devices use FSPI;
 * only one chip select may be active at a time. The LCD has no MISO lead. */
#define LCD_FLASH_PIN_SCK       12
#define LCD_FLASH_PIN_MOSI      11
#define LCD_FLASH_PIN_MISO      13
#define LCD_FLASH_PIN_LCD_CS    10
#define LCD_FLASH_PIN_FLASH_CS   6

#endif
