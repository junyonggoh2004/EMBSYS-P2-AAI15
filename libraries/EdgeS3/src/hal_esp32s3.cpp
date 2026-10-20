// hal_esp32s3.cpp
// ESP32-S3 backend for hal/hal.h (D1 tasks 12, 14-17), built on Arduino-ESP32.
// Behaviour follows the Pico backend (project_v3/platform/pico/hal_pico.c);
// differences are noted where the hardware forces them.
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <stdio.h>
#include "esp_timer.h"
#include "sdkconfig.h"

#include "hal/hal.h"
#include "platform_esp32s3.h"

// ---------- time ----------
// esp_timer_get_time() is a documented ESP-IDF exception: Arduino micros() is
// 32-bit and wraps after about 71 minutes, while the core relies on 64-bit time.

uint64_t hal_time_us(void) {
    return (uint64_t)esp_timer_get_time();
}

uint32_t hal_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------- delay ----------

void hal_delay_ms(uint32_t ms) {
    // delay() is vTaskDelay(), which can return up to one tick early;
    // finish on the timer so the caller always waits at least ms.
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    delay(ms);
    while (esp_timer_get_time() < end) delay(1);
}

void hal_delay_us(uint32_t us) {
    delayMicroseconds(us);
}

// ---------- console ----------

static ssize_t console_write(void *cookie, const char *buf, size_t len) {
    (void)cookie;
    return (ssize_t)Serial.write((const uint8_t *)buf, len);
}

void platform_console_begin(uint32_t wait_ms) {
    // The default 256-byte receive buffer drops the end of a long pasted line;
    // keep it well above the REPL's 512-byte line limit.
    Serial.setRxBufferSize(2048);
    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && millis() - start < wait_ms) delay(10);

    // Arduino sends printf to UART0, not the USB console, so give C stdio a
    // stream that writes to Serial. Tasks created later inherit it.
    cookie_io_functions_t io = { NULL, console_write, NULL, NULL };
    FILE *f = fopencookie(NULL, "w", io);
    if (!f) return;
    setvbuf(f, NULL, _IOLBF, 256);
    _GLOBAL_REENT->_stdout = f;
    stdout = f;
}

int hal_console_getc(uint32_t timeout_us) {
    int64_t end = esp_timer_get_time() + timeout_us;
    for (;;) {
        int ch = Serial.read();
        if (ch >= 0) return ch;
        if (esp_timer_get_time() >= end) return HAL_ERR_TIMEOUT;
        delay(1);
    }
}

// ---------- GPIO ----------

bool hal_gpio_valid(int pin) {
    if (pin < 0 || pin > 48) return false;
    if (pin >= 22 && pin <= 25) return false;   // not present on the ESP32-S3
    if (pin == 19 || pin == 20) return false;   // native USB, which carries the console
    if (pin >= 26 && pin <= 32) return false;   // SPI flash and quad PSRAM
#if CONFIG_SPIRAM_MODE_OCT
    if (pin >= 33 && pin <= 37) return false;   // octal PSRAM
#endif
    return true;
}

static uint64_t s_pwm_pins;   // pins currently attached to LEDC

int hal_gpio_mode(int pin, hal_gpio_mode_t mode) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    s_pwm_pins &= ~(1ULL << pin);   // pinMode() releases any LEDC attachment
    switch (mode) {
    case HAL_GPIO_OUTPUT:
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        break;
    case HAL_GPIO_INPUT_PULLUP:   pinMode(pin, INPUT_PULLUP);   break;
    case HAL_GPIO_INPUT_PULLDOWN: pinMode(pin, INPUT_PULLDOWN); break;
    default:                      pinMode(pin, INPUT);          break;
    }
    return HAL_OK;
}

int hal_gpio_write(int pin, int level) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    digitalWrite(pin, level ? HIGH : LOW);
    return HAL_OK;
}

int hal_gpio_read(int pin) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    return digitalRead(pin) ? 1 : 0;
}

int hal_gpio_toggle(int pin) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    // Arduino-ESP32 outputs keep their input buffer, so the pin can be read
    // back directly; the Pico has to rely on its input synchroniser instead.
    int next = !digitalRead(pin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, next ? HIGH : LOW);
    s_pwm_pins &= ~(1ULL << pin);
    return next;
}

// The Week 6 design ends a PULSE with a one-shot FreeRTOS software timer, so
// SchedulerTask never sleeps inside a rule action.
static TimerHandle_t s_pulse_timer[49];

static void pulse_end(TimerHandle_t t) {
    digitalWrite((int)(intptr_t)pvTimerGetTimerID(t), LOW);
}

int hal_gpio_pulse(int pin, uint32_t ms) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) ticks = 1;
    if (!s_pulse_timer[pin]) {
        s_pulse_timer[pin] = xTimerCreate("pulse", ticks, pdFALSE, (void *)(intptr_t)pin, pulse_end);
        if (!s_pulse_timer[pin]) return HAL_ERR_UNSUPPORTED;
    }
    hal_gpio_mode(pin, HAL_GPIO_OUTPUT);
    digitalWrite(pin, HIGH);
    xTimerChangePeriod(s_pulse_timer[pin], ticks, 0);   // starts, or restarts, the timer
    return HAL_OK;
}

// The reference firmware's echo measurement, moved here unchanged from the
// shared GPIO driver (D1 task 29): busy-wait for the echo to rise, then for it
// to fall, each bounded by timeout_us.
int hal_gpio_measure_pulse(int trig_pin, int echo_pin, uint32_t trig_us, uint32_t timeout_us)
{
    hal_gpio_mode(trig_pin, HAL_GPIO_OUTPUT);   // driven low
    hal_delay_us(2);

    // send trigger pulse
    hal_gpio_write(trig_pin, 1);
    hal_delay_us(trig_us);
    hal_gpio_write(trig_pin, 0);

    // wait for echo HIGH
    uint64_t t0 = hal_time_us();
    while (hal_gpio_read(echo_pin) <= 0) {
        if (hal_time_us() - t0 > timeout_us)
            return -1;
    }

    // echo HIGH -> measure width
    uint64_t start = hal_time_us();
    while (hal_gpio_read(echo_pin) > 0) {
        if (hal_time_us() - start > timeout_us)
            break;
    }
    uint64_t end = hal_time_us();
    int32_t width = (int32_t)(end - start);
    if (width < 0) width = 0;
    return (int)width; // microseconds
}

static volatile uint32_t s_edges[49];

static void IRAM_ATTR edge_isr(void *arg) {
    int pin = (int)(intptr_t)arg;
    s_edges[pin] = s_edges[pin] + 1;   // only this ISR writes it
}

int hal_gpio_count_edges(int pin, bool enable) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (enable) {
        s_edges[pin] = 0;
        attachInterruptArg(pin, edge_isr, (void *)(intptr_t)pin, CHANGE);
    } else {
        detachInterrupt(pin);
    }
    return HAL_OK;
}

uint32_t hal_gpio_edge_count(int pin) {
    return hal_gpio_valid(pin) ? s_edges[pin] : 0;
}

// ---------- PWM ----------

static const uint32_t kPwmFreqHz = 37463;   // Pico: 150 MHz / 4 / 1001
static const uint8_t  kPwmBits   = 10;

int hal_pwm_set(int pin, float duty) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    if (!(s_pwm_pins & (1ULL << pin))) {
        if (!ledcAttach(pin, kPwmFreqHz, kPwmBits)) return HAL_ERR_UNSUPPORTED;
        s_pwm_pins |= 1ULL << pin;
    }
    uint32_t level = (uint32_t)(duty * 1000.0f);   // same 1/1000 steps as the Pico
    ledcWrite(pin, (level * ((1u << kPwmBits) - 1) + 500) / 1000);
    return HAL_OK;
}

int hal_pwm_stop(int pin) {
    if (!hal_gpio_valid(pin)) return HAL_ERR_PIN;
    if (s_pwm_pins & (1ULL << pin)) {
        ledcDetach(pin);
        s_pwm_pins &= ~(1ULL << pin);
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    return HAL_OK;
}

// ---------- I2C ----------
// Two controllers (Wire, Wire1). A pin pair keeps its controller; a new pair
// takes a free one, or re-initialises the first if both are busy.

static TwoWire *const s_wire[2] = { &Wire, &Wire1 };
static int s_i2c_pins[2][2] = { { -1, -1 }, { -1, -1 } };

int hal_i2c_open(int sda, int scl, uint32_t baud) {
    if (!hal_gpio_valid(sda) || !hal_gpio_valid(scl)) return HAL_ERR_PIN;
    int bus = -1;
    for (int b = 0; b < 2 && bus < 0; b++)
        if (s_i2c_pins[b][0] == sda && s_i2c_pins[b][1] == scl) bus = b;
    for (int b = 0; b < 2 && bus < 0; b++)
        if (s_i2c_pins[b][0] < 0) bus = b;
    if (bus < 0) bus = 0;

    TwoWire *w = s_wire[bus];
    w->end();
    if (!w->begin(sda, scl, baud)) return HAL_ERR_UNSUPPORTED;   // internal pull-ups on
    s_i2c_pins[bus][0] = sda;
    s_i2c_pins[bus][1] = scl;
    return bus;
}

int hal_i2c_write(int bus, uint8_t addr, const uint8_t *data, size_t len, bool nostop) {
    TwoWire *w = s_wire[bus & 1];
    w->beginTransmission(addr);
    if (len) w->write(data, len);
    return w->endTransmission(!nostop) == 0 ? (int)len : HAL_ERR_IO;
}

int hal_i2c_read(int bus, uint8_t addr, uint8_t *buf, size_t len) {
    TwoWire *w = s_wire[bus & 1];
    size_t n = w->requestFrom(addr, len);
    if (n == 0) return HAL_ERR_IO;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)w->read();
    return (int)n;
}

// ---------- UART ----------
// Serial1 and Serial2 through the GPIO matrix; UART0 stays free for the
// boot console. Same port-choice rule as I2C.

static HardwareSerial *const s_uart[2] = { &Serial1, &Serial2 };
static int s_uart_pins[2][2] = { { -1, -1 }, { -1, -1 } };

static uint32_t uart_config(uint8_t bits, char parity, uint8_t stop) {
    static const uint32_t cfg[4][3][2] = {
        { { SERIAL_5N1, SERIAL_5N2 }, { SERIAL_5E1, SERIAL_5E2 }, { SERIAL_5O1, SERIAL_5O2 } },
        { { SERIAL_6N1, SERIAL_6N2 }, { SERIAL_6E1, SERIAL_6E2 }, { SERIAL_6O1, SERIAL_6O2 } },
        { { SERIAL_7N1, SERIAL_7N2 }, { SERIAL_7E1, SERIAL_7E2 }, { SERIAL_7O1, SERIAL_7O2 } },
        { { SERIAL_8N1, SERIAL_8N2 }, { SERIAL_8E1, SERIAL_8E2 }, { SERIAL_8O1, SERIAL_8O2 } },
    };
    int b = (bits >= 5 && bits <= 8) ? bits - 5 : 3;
    int p = (parity == 'E' || parity == 'e') ? 1 : (parity == 'O' || parity == 'o') ? 2 : 0;
    return cfg[b][p][stop == 2 ? 1 : 0];
}

int hal_uart_open(int tx, int rx, uint32_t baud, uint8_t bits, char parity, uint8_t stop) {
    if (!hal_gpio_valid(tx) || !hal_gpio_valid(rx)) return HAL_ERR_PIN;
    int port = -1;
    for (int u = 0; u < 2 && port < 0; u++)
        if (s_uart_pins[u][0] == tx && s_uart_pins[u][1] == rx) port = u;
    for (int u = 0; u < 2 && port < 0; u++)
        if (s_uart_pins[u][0] < 0) port = u;
    if (port < 0) port = 0;

    HardwareSerial *u = s_uart[port];
    u->end();
    u->begin(baud ? baud : 115200, uart_config(bits, parity, stop), rx, tx);
    s_uart_pins[port][0] = tx;
    s_uart_pins[port][1] = rx;
    return port;
}

int hal_uart_write(int port, const uint8_t *data, size_t len) {
    s_uart[port & 1]->write(data, len);
    return (int)len;
}

int hal_uart_read_byte(int port) {
    HardwareSerial *u = s_uart[port & 1];
    return u->available() > 0 ? u->read() : HAL_ERR_TIMEOUT;
}

// ---------- SPI ----------

typedef struct {
    bool active;
    int cs;
    uint32_t hz;
    uint8_t mode;
    uint8_t refs;
} spi_device_t;

static SPIClass s_fspi(FSPI);
static SemaphoreHandle_t s_spi_mutex;
static spi_device_t s_spi_devices[8];
static int s_spi_sclk = -1, s_spi_mosi = -1, s_spi_miso = -1;
static uint8_t s_spi_host_refs;

static bool spi_lock(uint32_t timeout_ms) {
    if (!s_spi_mutex) s_spi_mutex = xSemaphoreCreateMutex();
    if (!s_spi_mutex) return false;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (ticks == 0) ticks = 1;
    return xSemaphoreTake(s_spi_mutex, ticks) == pdTRUE;
}

static void spi_unlock(void) {
    xSemaphoreGive(s_spi_mutex);
}

int hal_spi_open(const char *host, int sclk, int mosi, int miso, int cs,
                 uint32_t hz, uint8_t mode) {
    if (!host || strcmp(host, "fspi") != 0 || mode > 3 || hz == 0)
        return HAL_ERR_INVALID;
    if (!hal_gpio_valid(sclk) || !hal_gpio_valid(mosi) || !hal_gpio_valid(cs) ||
        (miso >= 0 && !hal_gpio_valid(miso))) return HAL_ERR_PIN;
    if (!spi_lock(1000)) return HAL_ERR_NO_MEMORY;

    if (s_spi_host_refs &&
        (s_spi_sclk != sclk || s_spi_mosi != mosi || s_spi_miso != miso)) {
        spi_unlock();
        return HAL_ERR_BUSY;
    }
    for (int i = 0; i < 8; ++i) {
        if (!s_spi_devices[i].active || s_spi_devices[i].cs != cs) continue;
        if (s_spi_devices[i].hz != hz || s_spi_devices[i].mode != mode) {
            spi_unlock();
            return HAL_ERR_BUSY;
        }
        s_spi_devices[i].refs++;
        s_spi_host_refs++;
        spi_unlock();
        return i;
    }

    int slot = -1;
    for (int i = 0; i < 8; ++i) {
        if (!s_spi_devices[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        spi_unlock();
        return HAL_ERR_BUSY;
    }
    if (!s_spi_host_refs) {
        s_fspi.begin(sclk, miso, mosi, -1);
        s_spi_sclk = sclk;
        s_spi_mosi = mosi;
        s_spi_miso = miso;
    }
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    s_spi_devices[slot] = { true, cs, hz, mode, 1 };
    s_spi_host_refs++;
    spi_unlock();
    return slot;
}

int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx, size_t len,
                     uint32_t timeout_ms) {
    if (device < 0 || device >= 8 || len == 0 || len > 1024)
        return HAL_ERR_INVALID;
    if (!spi_lock(timeout_ms)) return HAL_ERR_TIMEOUT;
    if (!s_spi_devices[device].active) {
        spi_unlock();
        return HAL_ERR_INVALID;
    }

    const spi_device_t d = s_spi_devices[device];
    uint64_t start = hal_time_us();
    s_fspi.beginTransaction(SPISettings(d.hz, MSBFIRST, d.mode));
    digitalWrite(d.cs, LOW);
    for (size_t i = 0; i < len; ++i) {
        uint8_t v = s_fspi.transfer(tx ? tx[i] : 0);
        if (rx) rx[i] = v;
    }
    digitalWrite(d.cs, HIGH);
    s_fspi.endTransaction();
    uint64_t elapsed = hal_time_us() - start;
    spi_unlock();
    if (elapsed > (uint64_t)timeout_ms * 1000u) return HAL_ERR_TIMEOUT;
    return (int)len;
}

int hal_spi_close(int device) {
    if (device < 0 || device >= 8) return HAL_ERR_INVALID;
    if (!spi_lock(1000)) return HAL_ERR_TIMEOUT;
    spi_device_t *d = &s_spi_devices[device];
    if (!d->active || !d->refs) {
        spi_unlock();
        return HAL_ERR_INVALID;
    }
    d->refs--;
    s_spi_host_refs--;
    if (!d->refs) {
        digitalWrite(d->cs, HIGH);
        memset(d, 0, sizeof(*d));
    }
    if (!s_spi_host_refs) {
        s_fspi.end();
        s_spi_sclk = s_spi_mosi = s_spi_miso = -1;
    }
    spi_unlock();
    return HAL_OK;
}
