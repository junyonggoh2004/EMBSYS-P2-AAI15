// bus/uart.c — generic UART bus driver for the scheduler framework
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "bus/bus_common.h"   // app_cfg_t (contains uart_* fields)
#include "bus/bus_if.h"        // bus_t, sensor_cfg_t

/* ------------------------- Per-instance state ------------------------- */
typedef struct {
    // from cfg
    int       tx;
    int       rx;
    uint32_t  baud;
    uint8_t   bits;            // 5..8
    char      parity_ch;       // 'N','E','O'
    uint8_t   stop;            // 1 or 2
    uint16_t  read_len;        // max bytes to read per poll
    uint8_t   line_mode;       // 0 = raw, 1 = read until '\n'

    // optional "pre" request (sent once on init, or each poll if post_delay>0)
    uint8_t   pre[64];
    uint8_t   pre_len;
    uint32_t  post_delay_ms;
    bool      pre_done;

    // runtime
    uart_inst_t *uart;
} uart_ctx_t;

/* ------------------------- Helpers / mapping -------------------------- */
// RP2040 UART pin groups (TX/RX):
//  - uart0: {0,1}, {12,13}, {16,17}
//  - uart1: {4,5}, {8,9},   {20,21}
static uart_inst_t* pick_uart_for_pins(int tx, int rx) {
    // simple heuristic: if either pin belongs to an "uart1 pair", use uart1
    switch (tx) {
        case 4: case 5: case 8: case 9: case 20: case 21: return uart1;
        default: break;
    }
    switch (rx) {
        case 4: case 5: case 8: case 9: case 20: case 21: return uart1;
        default: break;
    }
    return uart0;
}

static uart_parity_t map_parity(char p) {
    if (p == 'E' || p == 'e') return UART_PARITY_EVEN;
    if (p == 'O' || p == 'o') return UART_PARITY_ODD;
    return UART_PARITY_NONE;
}

static void uart_setup(uart_ctx_t *c) {
    uart_init(c->uart, c->baud ? c->baud : 115200);
    uart_set_format(c->uart,
                    (c->bits >= 5 && c->bits <= 8) ? c->bits : 8,
                    (c->stop == 2) ? 2 : 1,
                    map_parity(c->parity_ch));
    uart_set_fifo_enabled(c->uart, true);

    gpio_set_function(c->tx, GPIO_FUNC_UART);
    gpio_set_function(c->rx, GPIO_FUNC_UART);
}

/* --------------------------- Pre sequence ----------------------------- */
static int do_pre_writes(uart_ctx_t *c) {
    if (!c->pre_len) return 0;
    // write blocking all pre bytes
    uart_write_blocking(c->uart, c->pre, c->pre_len);
    if (c->post_delay_ms) sleep_ms(c->post_delay_ms);
    return c->pre_len;
}

/* ---------------------- bus interface functions ----------------------- */
static int uart_init_cfg(const sensor_cfg_t *scfg, void **out_ctx) {
    const app_cfg_t *cfg = (const app_cfg_t *)scfg->kv_pairs;

    uart_ctx_t *c = (uart_ctx_t*)calloc(1, sizeof(*c));
    if (!c) return -1;

    c->tx          = cfg->uart_tx;
    c->rx          = cfg->uart_rx;
    c->baud        = cfg->uart_baud ? cfg->uart_baud : 115200;
    c->bits        = (uint8_t)(cfg->uart_bits ? cfg->uart_bits : 8);
    c->parity_ch   = cfg->uart_parity ? cfg->uart_parity : 'N';
    c->stop        = (uint8_t)(cfg->uart_stop ? cfg->uart_stop : 1);
    c->read_len    = cfg->uart_read_len ? cfg->uart_read_len : 64;
    c->line_mode   = (uint8_t)(cfg->uart_line_mode ? 1 : 0);

    c->pre_len     = cfg->uart_pre_len;
    if (c->pre_len > sizeof(c->pre)) c->pre_len = sizeof(c->pre);
    if (c->pre_len) memcpy(c->pre, cfg->uart_pre, c->pre_len);
    c->post_delay_ms = cfg->uart_post_delay_ms;
    c->pre_done    = false;

    c->uart = pick_uart_for_pins(c->tx, c->rx);
    uart_setup(c);

    // Optional: send pre once on init (good for wakeups)
    if (c->pre_len) {
        do_pre_writes(c);
        c->pre_done = true;
    }

    *out_ctx = c;
    return 0;
}

static int uart_probe(void *ctx_) {
    (void)ctx_;
    // Could try a non-intrusive check; UART has no "probe" per se.
    return 0;
}

static int uart_poll_once(void *ctx_, uint8_t *buf, size_t cap) {
    uart_ctx_t *c = (uart_ctx_t*)ctx_;
    if (!buf || cap == 0) return 0;

    // If a post_delay was requested, allow pre each poll (request/response devices)
    if (c->pre_len && (!c->pre_done || c->post_delay_ms > 0)) {
        do_pre_writes(c);
        c->pre_done = true;
    }

    const int want_max = (int)((c->read_len <= cap) ? c->read_len : cap);
    int n = 0;

    // We do a short, bounded read window to remain non-blocking at scheduler level.
    const int POLL_WINDOW_MS = 30;          // total read window (tweakable)
    const int QUIET_GAP_MS   = 2;           // break early if no bytes for this duration
    absolute_time_t t0 = get_absolute_time();
    absolute_time_t last_rx = t0;

    while (true) {
        // Consume all readily available bytes (bounded by want_max)
        while (n < want_max && uart_is_readable(c->uart)) {
            int ch = uart_getc(c->uart);
            buf[n++] = (uint8_t)ch;
            last_rx = get_absolute_time();

            if (c->line_mode && ch == '\n') {
                return n; // stop at newline in line mode
            }
        }

        if (n >= want_max) break;

        // Check timing windows
        absolute_time_t now = get_absolute_time();
        int64_t elapsed_ms = absolute_time_diff_us(t0, now) / 1000;
        if (elapsed_ms >= POLL_WINDOW_MS) break;

        int64_t quiet_ms = absolute_time_diff_us(last_rx, now) / 1000;
        if (quiet_ms >= QUIET_GAP_MS) break;

        // tiny nap to yield CPU but stay responsive
        sleep_us(200);
    }
    return n;  // 0..want_max
}

static void uart_stream_start(void *ctx_) {
    uart_ctx_t *c = (uart_ctx_t*)ctx_;
    (void)c;
    // Optional: flush RX FIFO so streaming starts clean
    while (uart_is_readable(c->uart)) (void)uart_getc(c->uart);
}

static void uart_stream_stop(void *ctx_) {
    (void)ctx_;
    // No special action needed
}

/* ----------------------------- Export --------------------------------- */
const bus_t bus_uart = {
    .name         = "uart",
    .init_cfg     = uart_init_cfg,
    .probe        = uart_probe,
    .poll_once    = uart_poll_once,
    .stream_start = uart_stream_start,
    .stream_stop  = uart_stream_stop
};


/*
BEGINCFG
name=LD2410_STREAM
proto=uart
mode=stream
freq_hz=0                 # ignored in stream mode
uart.tx=0                 # Pico UART0 TX pin (GP0). Change if you used UART1.
uart.rx=1                 # Pico UART0 RX pin (GP1). Change if you used UART1.
uart.baud=256000
uart.bits=8
uart.parity=N
uart.stop=1
uart.line_mode=0          # raw bytes (not line-delimited)
uart.read_len=128         # bytes to read per poll_once()
ENDCFG
*/

// BEGINCFG|name=LD2410_POLL|proto=uart|mode=poll|freq_hz=5|uart.tx=0

// BEGINCFG;name=LD2410_POLL;proto=uart;mode=poll;freq_hz=5;uart.tx=0