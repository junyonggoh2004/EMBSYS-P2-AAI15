#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "application/board_profile.h"

static bool fail(char *reason, size_t reason_size, const char *fmt, ...) {
    if (reason && reason_size) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(reason, reason_size, fmt, args);
        va_end(args);
    }
    return false;
}

static bool pin_is_reserved(int pin) {
    return (pin >= 10 && pin <= 15) ||  /* microSD */
           (pin >= 16 && pin <= 17) ||  /* ESP-01 socket */
           (pin >= 18 && pin <= 19) ||  /* buzzer/audio */
           (pin >= 20 && pin <= 22) ||  /* buttons */
           pin == 28;                   /* RGB LED */
}

static bool pin_is_available(int pin, char *reason, size_t reason_size) {
    if (pin < 0 || pin > 28)
        return fail(reason, reason_size, "GPIO%d is outside the Pico GPIO range", pin);
    if (pin_is_reserved(pin))
        return fail(reason, reason_size, "GPIO%d is reserved by the Maker Pi Pico carrier", pin);
    return true;
}

bool board_profile_validate_config(const app_cfg_t *cfg, char *reason, size_t reason_size) {
    if (!cfg)
        return fail(reason, reason_size, "missing configuration");
    if (!cfg->name[0])
        return fail(reason, reason_size, "sensor name is required");

    if (cfg->proto != proto_gpio)
        return true;

    const char *mode = cfg->gpio_mode ? cfg->gpio_mode : "digital";
    if (!strcmp(mode, "pulse")) {
        if (!cfg->gpio_trig_specified || !cfg->gpio_echo_specified)
            return fail(reason, reason_size, "pulse mode requires gpio.trig and gpio.echo");
        if (cfg->gpio_trig == cfg->gpio_echo)
            return fail(reason, reason_size, "gpio.trig and gpio.echo must use different pins");
        if (!pin_is_available(cfg->gpio_trig, reason, reason_size)) return false;
        return pin_is_available(cfg->gpio_echo, reason, reason_size);
    }

    if (!strcmp(mode, "digital") || !strcmp(mode, "counter") || !strcmp(mode, "onewire")) {
        if (!cfg->gpio_pin_specified)
            return fail(reason, reason_size, "%s mode requires gpio.pin", mode);
        return pin_is_available(cfg->gpio_pin, reason, reason_size);
    }

    return fail(reason, reason_size, "unsupported gpio.mode '%s'", mode);
}
