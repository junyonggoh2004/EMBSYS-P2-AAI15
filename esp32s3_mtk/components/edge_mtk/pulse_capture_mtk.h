/* Private implementation included after echo_disarm() in hal_mtk.c. Kept
 * separate so native tests exercise these exact functions with register/clock
 * boundaries replaced. All access is on the GPIO ISR's owning core. */
static bool s_capture_active;
static int s_capture_trig;
static uint64_t s_capture_begin;
static uint32_t s_capture_timeout;

static bool capture_owner(void)
{
    return tk_get_prc() == (ID)(s_gpio_core + 1);
}

void hal_gpio_capture_cancel(int echo)
{
    if (!capture_owner() || !s_capture_active || s_echo_pin != echo) return;
    echo_disarm(echo);
    echo_clear_status(echo);
    gpio_ll_set_level(s_gpio, s_capture_trig, 0);
    s_capture_active = false;
}

int hal_gpio_capture_start(int trig, int echo, uint32_t trig_us, uint32_t timeout_us)
{
    if (!hal_gpio_valid(trig) || !hal_gpio_valid(echo)) return HAL_ERR_PIN;
    if (trig == echo || !trig_us || trig_us > 1000 || !timeout_us ||
        timeout_us <= trig_us) return HAL_ERR_INVALID;
    // The active target must never silently fall back to a blocking call.
    if (!s_gpio_irq || s_echo_flg <= 0 || !capture_owner()) return HAL_ERR_IO;
    if (s_capture_active || s_echo_pin >= 0 ||
        (s_count_mask & ((1ULL << echo) | (1ULL << trig)))) return HAL_ERR_BUSY;
    if (gpio_ll_get_level(s_gpio, echo)) return HAL_ERR_BUSY;

    int rc = hal_gpio_mode(trig, HAL_GPIO_OUTPUT);
    if (rc != HAL_OK) return rc;
    gpio_ll_intr_disable(s_gpio, echo);
    s_echo_rise_us = s_echo_fall_us = 0;
    tk_clr_flg(s_echo_flg, 0);
    s_echo_pin = echo;
    s_capture_trig = trig;
    s_capture_timeout = timeout_us;
    s_capture_begin = hal_time_us();
    s_capture_active = true;
    gpio_ll_set_intr_type(s_gpio, echo, GPIO_INTR_POSEDGE);
    echo_clear_status(echo);
    gpio_ll_intr_enable_on_core(s_gpio, s_gpio_core, echo);
    gpio_ll_set_level(s_gpio, trig, 1);
    esp_rom_delay_us(trig_us);
    gpio_ll_set_level(s_gpio, trig, 0);
    return HAL_OK;
}

int hal_gpio_capture_poll(int echo, hal_pulse_result_t *result)
{
    if (!result) return HAL_ERR_INVALID;
    memset(result, 0, sizeof *result);
    if (!capture_owner()) return HAL_ERR_IO;
    if (!s_capture_active || s_echo_pin != echo) return HAL_ERR_INVALID;
    // Interrupt masking gives a consistent pair of 64-bit timestamps on this
    // 32-bit CPU. The ISR is pinned to this same core; no cross-core spinlock.
    gpio_ll_intr_disable(s_gpio, echo);
    uint64_t rise = s_echo_rise_us, fall = s_echo_fall_us;
    uint64_t now = hal_time_us();
    uint64_t deadline = s_capture_begin + s_capture_timeout;
    if (fall) {
        int rc = HAL_ERR_TIMEOUT;
        if (rise >= s_capture_begin && fall > rise && fall <= deadline) {
            result->rise_us = rise;
            result->fall_us = fall;
            result->width_us = (uint32_t)(fall - rise);
            rc = HAL_OK;
        }
        hal_gpio_capture_cancel(echo);
        return rc;
    }
    if (now >= deadline) {
        hal_gpio_capture_cancel(echo);
        return HAL_ERR_TIMEOUT;
    }
    gpio_ll_intr_enable_on_core(s_gpio, s_gpio_core, echo);
    return HAL_PULSE_PENDING;
}
