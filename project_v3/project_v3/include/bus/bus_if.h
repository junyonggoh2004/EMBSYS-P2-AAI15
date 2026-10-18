#ifndef BUS_IF_H
#define BUS_IF_H

#include <stddef.h>
#include <stdint.h>

typedef struct sensor_cfg {
    const char *name;
    const char *proto;
    void *kv_pairs;        // key/value linked list or simple storage (depends on your parser)
} sensor_cfg_t;

typedef struct {
    const char *name;
    int  (*init_cfg)(const sensor_cfg_t *cfg, void **ctx_out);
    int  (*probe)(void *ctx);
    int  (*poll_once)(void *ctx, uint8_t *buf, size_t cap);
    void (*stream_start)(void *ctx);
    void (*stream_stop)(void *ctx);
} bus_t;

const bus_t *bus_lookup(const char *name);
extern const bus_t bus_i2c;
extern const bus_t bus_uart;
extern const bus_t bus_gpio;

#endif
