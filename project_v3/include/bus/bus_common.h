// include/bus/bus_common.h
#ifndef BUS_COMMON_H
#define BUS_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"
// Milliseconds since boot (monotonic)
static inline uint32_t app_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}
// ---------- enums ----------
typedef enum {
    proto_i2c  = 0,
    proto_uart = 1,
    proto_gpio = 2,
    proto_mqtt = 3
} proto_t;

typedef enum {
    sample_mode_poll   = 0,
    sample_mode_stream = 1
} sample_mode_t;

// ---------- global parsed config ----------
typedef struct {
    // common
    proto_t        proto;
    sample_mode_t  mode;
    char           name[32];      // <-- array so REPL can write into it
    float       freq_hz;

    // -------- I2C --------
    int            i2c_sda;
    int            i2c_scl;
    uint32_t       i2c_baud;
    int            i2c_addr;
    uint32_t       i2c_reg;
    int            i2c_reg_size;       // 0..4
    uint16_t       i2c_read_len;
    int            i2c_restart;        // 0/1

    uint8_t        i2c_pre[32];         
    uint8_t        i2c_pre_len;        

    uint32_t       i2c_post_delay_ms;

    // -------- UART --------
    int            uart_tx;
    int            uart_rx;
    uint32_t       uart_baud;
    int            uart_bits;
    char           uart_parity;        // 'N','E','O'
    int            uart_stop;
    uint16_t       uart_read_len;
    int            uart_line_mode;     // 0/1

    uint8_t        uart_pre[64];       
    uint8_t        uart_pre_len;       
    uint32_t       uart_post_delay_ms;
    bool           uart_pre_done; 

    // -------- GPIO --------
    const char    *gpio_mode;          // "digital" | "pulse" | "onewire" | "counter"
    int            gpio_pin;
    const char    *gpio_pull;          // "up" | "down" | "off"
    int            gpio_invert;
    int            gpio_debounce_ms;
    int            gpio_trig;
    int            gpio_echo;
    int            gpio_trig_us;
    int            gpio_pulse_timeout_us;
    int            gpio_pulse_guard_ms;
    bool           gpio_pin_specified;
    bool           gpio_trig_specified;
    bool           gpio_echo_specified;

    // -------- MQTT --------
    char      remote_node[32];   // node to follow
    char      remote_source[32]; // sensor name on that node
} app_cfg_t;

// ---------- scheduler ----------
void scheduler_apply_config(const app_cfg_t *cfg);
void scheduler_run_step(const app_cfg_t *cfg, bool running);

// ---------- publisher ----------
void publisher_emit_hex(const app_cfg_t *cfg,
                        const char *proto_name,
                        const char *sensor_name,
                        const uint8_t *data, int len);

// ---------- config parser (used by repl.c) ----------
void cfg_parse_begin(void);
void cfg_parse_line(const char *line, app_cfg_t *cfg);
void cfg_parse_end(app_cfg_t *cfg);

// ---------- REPL (used by main.c) ----------
void repl_init_defaults(app_cfg_t *cfg);
void repl_show_help(void);
void repl_print_cfg(const app_cfg_t *cfg, bool running);
void repl_poll(app_cfg_t *cfg, bool *running, bool *cfg_ready);

#endif // BUS_COMMON_H
