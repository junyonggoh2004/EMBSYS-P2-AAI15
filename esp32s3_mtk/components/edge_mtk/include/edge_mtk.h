// edge_mtk.h
// ESP32-S3 platform services on µT-Kernel 3 SMP that are not part of the
// shared HAL (hal/hal.h): console set-up, HAL start-up and test hooks.
#ifndef EDGE_MTK_H
#define EDGE_MTK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif

// Console (console_mtk.c). console_init() creates the console mutex and flag;
// call it first in usermain. console_start_rx() registers the USB receive
// interrupt on the calling core; call it once from the console reader's task.
ER   console_init(void);
ER   console_start_rx(void);
void con_write(const char *p, size_t n);
void console_stats(uint32_t *rx_irqs, uint32_t *rx_dropped);

// HAL start-up (hal_mtk.c): creates the PULSE alarm handlers and registers the
// GPIO interrupt on the calling core. Call once from SchedulerTask (core 1).
ER   hal_mtk_init(void);
uint32_t hal_mtk_gpio_core(void);
void hal_mtk_set_gpio_hook(void (*hook)(uint32_t lo, uint32_t hi));
void hal_mtk_uart_internal_loopback(int port, bool on);
uint32_t hal_mtk_uart_dropped(int port);

#ifdef __cplusplus
}
#endif

#endif // EDGE_MTK_H
