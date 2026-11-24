/* D2-2 and D2-4: the real SPI bus driver (bus/spi.c) against a modelled HAL.
 * The HAL records what the driver asks for and plays a W25Q32 that answers the
 * read-ID command 0x9F with EF 40 16, the chip on Board B and in README 5.4.
 * Checks: the device is opened with the configured host, pins, clock and mode;
 * one poll sends the configured bytes and returns exactly the bytes read; a HAL
 * error is passed up and never becomes a reading; a TX-only transfer reads
 * nothing; a read longer than the caller's buffer is refused; the device is
 * closed when the sensor is removed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hal/hal.h"
#include "bus/bus_common.h"
#include "bus/bus_if.h"

extern const bus_t bus_spi;

static struct { char host[8]; int sclk, mosi, miso, cs; uint32_t hz; uint8_t mode; } opened;
static int open_rc, transfer_rc, opens, closes, transfers, closed_device;
static uint8_t last_tx[APP_SPI_RX_MAX];
static size_t last_len;
static bool last_rx_null;
static uint32_t last_timeout;

int hal_spi_open(const char *host, int sclk, int mosi, int miso, int cs, uint32_t hz, uint8_t mode)
{
    ++opens;
    snprintf(opened.host, sizeof opened.host, "%s", host);
    opened.sclk = sclk; opened.mosi = mosi; opened.miso = miso; opened.cs = cs;
    opened.hz = hz; opened.mode = mode;
    return open_rc;
}
int hal_spi_transfer(int device, const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms)
{
    assert(device == 3);
    ++transfers;
    memcpy(last_tx, tx, len);
    last_len = len;
    last_rx_null = rx == NULL;
    last_timeout = timeout_ms;
    if (transfer_rc < 0) return transfer_rc;
    if (rx) {
        memset(rx, 0xFF, len);                      /* MISO floats high while the command goes out */
        if (tx[0] == 0x9F && len >= 4) { rx[1] = 0xEF; rx[2] = 0x40; rx[3] = 0x16; }
    }
    return (int)len;
}
int hal_spi_close(int device) { ++closes; closed_device = device; return HAL_OK; }

/* README 5.4's FLASH_ID line, as the config parser stores it. */
static app_cfg_t flash_id(void)
{
    app_cfg_t c;
    memset(&c, 0, sizeof c);
    snprintf(c.name, sizeof c.name, "FLASH_ID");
    c.proto = proto_spi;
    snprintf(c.spi_host, sizeof c.spi_host, "fspi");
    c.spi_sclk = 11; c.spi_mosi = 12; c.spi_miso = 13; c.spi_cs = 10;
    c.spi_hz = 1000000; c.spi_mode = 0;
    c.spi_tx[0] = 0x9F; c.spi_tx_len = 1;
    c.spi_read_len = 4; c.spi_timeout_ms = 100;
    return c;
}

int main(void)
{
    app_cfg_t cfg = flash_id();
    sensor_cfg_t scfg = {0};
    scfg.kv_pairs = &cfg;
    void *ctx = NULL;
    uint8_t buf[8];

    /* Opened exactly as configured. */
    open_rc = 3;
    assert(bus_spi.init_cfg(&scfg, &ctx) == HAL_OK && ctx && opens == 1);
    assert(!strcmp(opened.host, "fspi") && opened.sclk == 11 && opened.mosi == 12 && opened.miso == 13 &&
           opened.cs == 10 && opened.hz == 1000000 && opened.mode == 0);
    assert(bus_spi.probe(ctx) == HAL_OK);

    /* One poll: send 9F, read four bytes, return them unchanged. */
    memset(buf, 0, sizeof buf);
    assert(bus_spi.poll_once(ctx, buf, sizeof buf) == 4);
    assert(transfers == 1 && last_len == 4 && last_tx[0] == 0x9F && !last_rx_null && last_timeout == 100);
    assert(buf[0] == 0xFF && buf[1] == 0xEF && buf[2] == 0x40 && buf[3] == 0x16);

    /* A HAL error is passed up; nothing is copied as a reading. */
    memset(buf, 0xAA, sizeof buf);
    transfer_rc = HAL_ERR_TIMEOUT;
    assert(bus_spi.poll_once(ctx, buf, sizeof buf) == HAL_ERR_TIMEOUT && buf[0] == 0xAA);
    transfer_rc = 0;

    /* The caller's buffer is too small for the configured read. */
    assert(bus_spi.poll_once(ctx, buf, 3) == HAL_ERR_INVALID && transfers == 2);

    /* Removing the sensor closes its device. */
    bus_spi.deinit(ctx);
    assert(closes == 1 && closed_device == 3);

    /* TX-only: two bytes out, no receive buffer, nothing returned. */
    cfg = flash_id();
    cfg.spi_tx[0] = 0x06; cfg.spi_tx[1] = 0x04; cfg.spi_tx_len = 2; cfg.spi_read_len = 0;
    ctx = NULL;
    assert(bus_spi.init_cfg(&scfg, &ctx) == HAL_OK);
    assert(bus_spi.poll_once(ctx, buf, sizeof buf) == 0 && last_len == 2 && last_rx_null &&
           last_tx[0] == 0x06 && last_tx[1] == 0x04);
    bus_spi.deinit(ctx);

    /* The host cannot be opened: the error is returned and no device is left. */
    open_rc = HAL_ERR_BUSY;
    ctx = (void *)1;
    assert(bus_spi.init_cfg(&scfg, &ctx) == HAL_ERR_BUSY && ctx == NULL && closes == 2);

    puts("SPI driver: PASS (opened as configured, 9F read returns EF 40 16, errors passed up, TX-only, short buffer refused, closed on removal)");
    return 0;
}
