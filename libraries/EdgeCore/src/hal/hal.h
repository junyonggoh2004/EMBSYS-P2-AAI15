// include/hal/hal.h
// Platform services for the shared framework code (D1 task 11).
//
// Shared modules (configuration, scheduler, REPL, rules, MQTT payload handling,
// inference manager) call these instead of the Pico SDK or Arduino APIs.
// Each target implements them once: project_v3/platform/pico/hal_pico.c for
// the Pico 2 W and esp32s3_edge/src/hal_esp32s3.cpp for the ESP32-S3.
//
// Conventions
//   Functions that can fail return HAL_OK (0) or a negative HAL_ERR_* code.
//   No function blocks longer than its comment states.
//   Unless marked ISR-safe, a function may be called from any task but not
//   from an interrupt handler.
//   Console output uses the C library (printf, puts, putchar). Each backend
//   routes stdout to its console: the Pico SDK's USB stdio on the Pico.
//
// Drivers below the boundary (bus/gpio.c, bus/i2c.c, bus/uart.c, the MQTT
// client, flash storage, the TFLite Micro wrapper) are platform code and may
// use their SDK directly; see ARCHITECTURE.md section 4.
#ifndef HAL_HAL_H
#define HAL_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_OK               0
#define HAL_ERR_TIMEOUT     (-1)  // nothing arrived within the timeout
#define HAL_ERR_PIN         (-2)  // pin does not exist or is reserved on this board
#define HAL_ERR_UNSUPPORTED (-3)  // operation not available on this target
#define HAL_ERR_IO          (-4)  // the bus device did not respond

// ---------- time ----------

// Microseconds since boot. Monotonic and 64-bit, so it does not wrap in
// practice. Never blocks. ISR-safe.
uint64_t hal_time_us(void);

// Milliseconds since boot, truncated to 32 bits (wraps after about 49.7 days).
// Never blocks. ISR-safe.
uint32_t hal_time_ms(void);

// ---------- delay ----------

// Blocks the caller for at least ms milliseconds. On an RTOS target other
// tasks run meanwhile. Must not be called from an ISR or while holding a lock.
void hal_delay_ms(uint32_t ms);

// Busy-waits for at least us microseconds, us <= 1000. Sensor timing only.
void hal_delay_us(uint32_t us);

// ---------- console input ----------

// Next byte from the console (0..255), or HAL_ERR_TIMEOUT if none arrives
// within timeout_us. A timeout of 0 polls without waiting. One reader only.
int hal_console_getc(uint32_t timeout_us);

// ---------- GPIO ----------

typedef enum {
    HAL_GPIO_INPUT = 0,
    HAL_GPIO_INPUT_PULLUP,
    HAL_GPIO_INPUT_PULLDOWN,
    HAL_GPIO_OUTPUT            // pull state left unchanged
} hal_gpio_mode_t;

// True if pin exists on this board and is free for application use (not the
// console, flash or PSRAM pins).
bool hal_gpio_valid(int pin);

// Sets the pin's direction and pull. Returns HAL_OK or HAL_ERR_PIN.
// Configuring an output resets its level to 0 until hal_gpio_write().
int hal_gpio_mode(int pin, hal_gpio_mode_t mode);

// Drives an output pin to level (0 or nonzero). Returns HAL_OK or HAL_ERR_PIN.
int hal_gpio_write(int pin, int level);

// Reads the pin's input level: 0, 1, or HAL_ERR_PIN.
int hal_gpio_read(int pin);

// Makes pin an output and drives it to the opposite of the level it reads
// before switching. Returns the new level (0 or 1) or HAL_ERR_PIN.
int hal_gpio_toggle(int pin);

// Drives pin high for ms milliseconds, then low. On an RTOS target it returns
// at once and a one-shot timer ends the pulse, so the caller never sleeps in
// a rule action; the Pico keeps the reference behaviour and blocks for ms.
// Returns HAL_OK, HAL_ERR_PIN, or HAL_ERR_UNSUPPORTED if no timer is free.
int hal_gpio_pulse(int pin, uint32_t ms);

// Counts both edges on pin from an interrupt, so no edge is lost between
// samples. enable=false stops counting. Returns HAL_OK, HAL_ERR_PIN, or
// HAL_ERR_UNSUPPORTED if the pin cannot interrupt.
int hal_gpio_count_edges(int pin, bool enable);

// Edges counted on pin since counting was last enabled. Never blocks; the
// 32-bit read is atomic. ISR-safe.
uint32_t hal_gpio_edge_count(int pin);

// ---------- PWM ----------

// Starts PWM on pin with duty in 0.0..1.0 (clamped), resolution 1/1000.
// The frequency is fixed per target so that parity tests match: on the
// Pico 2 W it is clk_sys / 4 / 1001, about 37.5 kHz at 150 MHz.
// Returns HAL_OK, HAL_ERR_PIN, or HAL_ERR_UNSUPPORTED if no PWM channel is free.
int hal_pwm_set(int pin, float duty);

// Stops PWM and leaves pin as an output driven low. Returns HAL_OK or HAL_ERR_PIN.
int hal_pwm_stop(int pin);

// ---------- I2C (used by bus/i2c.c) ----------

// Opens an I2C controller on sda/scl at baud with the internal pull-ups on.
// Opening the same pins again re-initialises that controller. Returns a bus
// handle (>= 0), HAL_ERR_PIN, or HAL_ERR_UNSUPPORTED if no controller is free.
int hal_i2c_open(int sda, int scl, uint32_t baud);

// Writes len bytes to the 7-bit address. nostop keeps the bus for a repeated
// start. Returns len, or HAL_ERR_IO if the device does not acknowledge.
// Blocks for the transfer only (bounded by the bus timeout).
int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool nostop);

// Reads len bytes from the 7-bit address. Returns the number read, or
// HAL_ERR_IO if the device does not acknowledge.
int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len);

// ---------- UART (used by bus/uart.c) ----------

// Opens a UART on tx/rx with the frame format (bits 5..8, parity 'N'/'E'/'O',
// stop 1 or 2). Returns a port handle (>= 0), HAL_ERR_PIN, or
// HAL_ERR_UNSUPPORTED if no UART is free.
int hal_uart_open(int tx, int rx, uint32_t baud, uint8_t bits, char parity, uint8_t stop);

// Queues len bytes for transmission, blocking until they fit. Returns len.
int hal_uart_write(int port, const uint8_t *data, size_t len);

// Next received byte (0..255), or HAL_ERR_TIMEOUT if none is waiting.
// Never blocks.
int hal_uart_read_byte(int port);

#ifdef __cplusplus
}
#endif

#endif // HAL_HAL_H
