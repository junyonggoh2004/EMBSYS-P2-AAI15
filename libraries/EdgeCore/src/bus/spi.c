#include <stdlib.h>
#include <string.h>

#include "hal/hal.h"
#include "bus/bus_common.h"
#include "bus/bus_if.h"

typedef struct {
    int device;
    uint8_t tx[APP_SPI_TX_MAX];
    uint8_t tx_len;
    uint16_t read_len;
    uint32_t timeout_ms;
} spi_ctx_t;

static int spi_init_cfg(const sensor_cfg_t *scfg, void **out_ctx) {
    if (!scfg || !out_ctx || !scfg->kv_pairs) return HAL_ERR_INVALID;
    *out_ctx = NULL;
    const app_cfg_t *cfg = (const app_cfg_t *)scfg->kv_pairs;

    spi_ctx_t *ctx = (spi_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return HAL_ERR_NO_MEMORY;
    ctx->device = -1;
    ctx->tx_len = cfg->spi_tx_len;
    ctx->read_len = cfg->spi_read_len;
    ctx->timeout_ms = cfg->spi_timeout_ms;
    memcpy(ctx->tx, cfg->spi_tx, ctx->tx_len);

    ctx->device = hal_spi_open(cfg->spi_host, cfg->spi_sclk, cfg->spi_mosi,
                               cfg->spi_miso, cfg->spi_cs, cfg->spi_hz,
                               cfg->spi_mode);
    if (ctx->device < 0) {
        int err = ctx->device;
        free(ctx);
        return err;
    }
    *out_ctx = ctx;
    return HAL_OK;
}

static void spi_deinit(void *ctx_) {
    spi_ctx_t *ctx = (spi_ctx_t *)ctx_;
    if (!ctx) return;
    if (ctx->device >= 0) (void)hal_spi_close(ctx->device);
    free(ctx);
}

static int spi_probe(void *ctx_) {
    return ctx_ ? HAL_OK : HAL_ERR_INVALID;
}

static int spi_poll_once(void *ctx_, uint8_t *buf, size_t cap) {
    spi_ctx_t *ctx = (spi_ctx_t *)ctx_;
    if (!ctx) return HAL_ERR_INVALID;
    if (ctx->read_len > cap) return HAL_ERR_INVALID;

    size_t len = ctx->tx_len > ctx->read_len ? ctx->tx_len : ctx->read_len;
    if (len == 0 || len > APP_SPI_RX_MAX) return HAL_ERR_INVALID;

    uint8_t tx[APP_SPI_RX_MAX] = {0};
    uint8_t rx[APP_SPI_RX_MAX];
    memcpy(tx, ctx->tx, ctx->tx_len);
    int r = hal_spi_transfer(ctx->device, tx,
                             ctx->read_len ? rx : NULL, len,
                             ctx->timeout_ms);
    if (r < 0) return r;
    if (!ctx->read_len) return 0;
    memcpy(buf, rx, ctx->read_len);
    return (int)ctx->read_len;
}

static void spi_stream_start(void *ctx) { (void)ctx; }
static void spi_stream_stop(void *ctx)  { (void)ctx; }

const bus_t bus_spi = {
    .name         = "spi",
    .init_cfg     = spi_init_cfg,
    .deinit       = spi_deinit,
    .probe        = spi_probe,
    .poll_once    = spi_poll_once,
    .stream_start = spi_stream_start,
    .stream_stop  = spi_stream_stop
};
