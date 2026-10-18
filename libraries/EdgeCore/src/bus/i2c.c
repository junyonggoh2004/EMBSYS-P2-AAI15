#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hal/hal.h"
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
    int      bus;           // hal_i2c_open() handle

    uint8_t  pre[32];
    uint8_t  pre_len;
    uint16_t post_delay_ms;

    bool pre_done;   // true if we’ve already executed pre at init-time
} i2c_ctx_t;

/* ---------- forward declarations ---------- */
static int do_pre_writes(i2c_ctx_t *c);

/* ---------- helpers ---------- */
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

    c->pre_len        = cfg->i2c_pre_len;
    if (c->pre_len > sizeof(c->pre)) c->pre_len = sizeof(c->pre);
    memcpy(c->pre, cfg->i2c_pre, c->pre_len);
    printf("[I2C] pre_len=%u :", c->pre_len);
    for (uint8_t i = 0; i < c->pre_len; ++i) printf(" %02X", c->pre[i]);
    printf("\n");

    c->post_delay_ms  = cfg->i2c_post_delay_ms;

    c->bus = hal_i2c_open(c->sda, c->scl, c->baud);
    if (c->bus < 0) {
        printf("[I2C] cannot open bus on SDA %d SCL %d (err=%d)\n", c->sda, c->scl, c->bus);
        int err = c->bus;
        free(c);
        return err;
    }

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

        int r = hal_i2c_write(c->bus, c->addr, chunk, (size_t)sz, false);
        if (r < 0) {
            printf("[I2C] pre write failed (sz=%d err=%d)\n", sz, r);
            return r;
        }
        hal_delay_ms(2);
    }
    if (c->post_delay_ms) hal_delay_ms(c->post_delay_ms);
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

    r = hal_i2c_write(c->bus, c->addr, regbuf, c->reg_size, c->restart);
    if (r < 0) { printf("[I2C] write_blocking failed err=%d\n", r); return r; }

    int to_read = (int)((c->read_len <= cap) ? c->read_len : cap);
    if (to_read <= 0) return 0;

    r = hal_i2c_read(c->bus, c->addr, buf, (size_t)to_read);
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
