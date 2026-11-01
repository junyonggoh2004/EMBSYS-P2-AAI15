#include "hal/hal.h"
__attribute__((weak)) uint32_t hal_uart_rx_dropped(int port)
{ (void)port; return UINT32_MAX; }
