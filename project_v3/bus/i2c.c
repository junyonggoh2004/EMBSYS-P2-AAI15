#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "bus/bus_common.h"
#include "bus/bus_if.h"

/* Per-instance state */
typedef struct {
    // from cfg
    int sda, scl;
    uint32_t baud;
    uint8_t addr;
    uint32_t reg;
    uint8_t reg_size;
    uint16_t read_len;
    bool restart;

    // runtime
    i2c_inst_t *i2c;

    uint8_t  pre[32];
    uint8_t  pre_len;
    uint16_t post_delay_ms;

    bool pre_done;   // true if we’ve already executed pre at init-time
} i2c_ctx_t;

/* ---------- forward declarations ---------- */
static int do_pre_writes(i2c_ctx_t *c);

/* ---------- helpers ---------- */
static i2c_inst_t *pick_i2c_for_pins(int sda, int scl) {
    (void)scl; // heuristic is based on the SDA pin group
    // i2c1 groups: {2,3},{6,7},{10,11},{14,15},{18,19},{26,27}
    // i2c0 groups: {0,1},{4,5},{8,9},{12,13},{16,17},{20,21}
    switch (sda) {
        case 2: case 3: case 6: case 7: case 10: case 11:
        case 14: case 15: case 18: case 19: case 26: case 27:
            return i2c1;
        default:
            return i2c0;
    }
}

static void i2c_setup_pins(i2c_ctx_t *c) {
    i2c_init(c->i2c, c->baud);
    gpio_set_function(c->sda, GPIO_FUNC_I2C);
    gpio_set_function(c->scl, GPIO_FUNC_I2C);
    gpio_pull_up(c->sda);
    gpio_pull_up(c->scl);
}

/* ---------- bus implementation ---------- */
static int i2c_init_cfg(const sensor_cfg_t *scfg, void **out_ctx) {
    const app_cfg_t *cfg = (const app_cfg_t *)scfg->kv_pairs;

    i2c_ctx_t *c = (i2c_ctx_t *)calloc(1, sizeof(*c));
    if (!c) return -1;

    c->sda       = cfg->i2c_sda;
    c->scl       = cfg->i2c_scl;
    c->baud      = cfg->i2c_baud ? cfg->i2c_baud : 100000;
    c->addr      = (uint8_t)cfg->i2c_addr;
    c->reg       = cfg->i2c_reg;
    c->reg_size  = (uint8_t)cfg->i2c_reg_size;
    c->read_len  = cfg->i2c_read_len;
    c->restart   = cfg->i2c_restart != 0;

    c->i2c            = pick_i2c_for_pins(c->sda, c->scl);
    c->pre_len        = cfg->i2c_pre_len;
    if (c->pre_len > sizeof(c->pre)) c->pre_len = sizeof(c->pre);
    memcpy(c->pre, cfg->i2c_pre, c->pre_len);
    printf("[I2C] pre_len=%u :", c->pre_len);
    for (uint8_t i = 0; i < c->pre_len; ++i) printf(" %02X", c->pre[i]);
    printf("\n");

    c->post_delay_ms  = cfg->i2c_post_delay_ms;

    i2c_setup_pins(c);

    // Run pre once at init for setup-type sensors
    if (c->pre_len) {
        int r = do_pre_writes(c);
        if (r < 0) {
            printf("[I2C] init pre-writes failed r=%d\n", r);
            free(c);
            return r;
        }
        c->pre_done = true;
    }

    *out_ctx = c;
    return 0;
}

static int i2c_probe(void *ctx_) {
    (void)ctx_;
    return 0;
}

static int do_pre_writes(i2c_ctx_t *c) {
    if (!c->pre_len) return 0;
    
    // Treat as (reg,value) pairs; odd leftover byte is a single write.
    for (uint8_t i = 0; i < c->pre_len; ) {
        uint8_t chunk[2];
        int sz = 0;
        chunk[sz++] = c->pre[i++];
        if (i < c->pre_len) chunk[sz++] = c->pre[i++];

        int r = i2c_write_blocking(c->i2c, c->addr, chunk, sz, false);
        if (r < 0) {
            printf("[I2C] pre write failed (sz=%d err=%d)\n", sz, r);
            return r;
        }
        sleep_ms(2);
    }
    if (c->post_delay_ms) sleep_ms(c->post_delay_ms);
    return 0;
}

static int i2c_poll_once(void *ctx_, uint8_t *buf, size_t cap) {
    i2c_ctx_t *c = (i2c_ctx_t*)ctx_;
    int r;  // <-- define r

    // Only re-run pre per poll if explicitly implied by post_delay_ms>0
    if (c->pre_len && (!c->pre_done || c->post_delay_ms > 0)) {
        r = do_pre_writes(c);
        if (r < 0) return r;
        if (c->post_delay_ms == 0) c->pre_done = true; // pre was one-shot
    }

    // Standard register write (size 1..4) then read
    uint8_t regbuf[4];
    for (int i = 0; i < c->reg_size; ++i)
        regbuf[c->reg_size - 1 - i] = (uint8_t)((c->reg >> (8*i)) & 0xFF);

    r = i2c_write_blocking(c->i2c, c->addr, regbuf, c->reg_size, c->restart);
    if (r < 0) { printf("[I2C] write_blocking failed err=%d\n", r); return r; }

    int to_read = (int)((c->read_len <= cap) ? c->read_len : cap);
    if (to_read <= 0) return 0;

    r = i2c_read_blocking(c->i2c, c->addr, buf, to_read, false);
    if (r < 0) { printf("[I2C] read_blocking failed err=%d\n", r); return r; }
    return r;
}

static void i2c_stream_start(void *ctx) { (void)ctx; }
static void i2c_stream_stop(void *ctx)  { (void)ctx; }

const bus_t bus_i2c = {
    .name = "i2c",
    .init_cfg = i2c_init_cfg,
    .probe = i2c_probe,
    .poll_once = i2c_poll_once,
    .stream_start = i2c_stream_start,
    .stream_stop = i2c_stream_stop
};
