#include "hal/hal.h"

/* Optional backend. Pico/Arduino keep their measured synchronous baseline;
 * the active micro T-Kernel HAL supplies strong asynchronous definitions. */
__attribute__((weak)) int hal_gpio_capture_start(int trig, int echo,
                                                uint32_t trig_us, uint32_t timeout_us)
{
    (void)trig; (void)echo; (void)trig_us; (void)timeout_us;
    return HAL_ERR_UNSUPPORTED;
}
__attribute__((weak)) int hal_gpio_capture_poll(int echo, hal_pulse_result_t *result)
{
    (void)echo; (void)result;
    return HAL_ERR_UNSUPPORTED;
}
__attribute__((weak)) void hal_gpio_capture_cancel(int echo) { (void)echo; }
