#include "tcrt5000.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

typedef struct {
    bool initialized;
    bool active_low;
    gpio_num_t digital_pin;
    adc_channel_t adc_channel;
    adc_oneshot_unit_handle_t adc;
    adc_cali_handle_t calibration;
    bool calibrated;
} tcrt5000_context_t;

static tcrt5000_context_t s_tcrt;

app_status_t tcrt5000_init(const hardware_config_t *hardware,
                           const device_config_t *device)
{
    if (hardware == NULL || device == NULL) return APP_STATUS_INVALID_ARGUMENT;
    memset(&s_tcrt, 0, sizeof(s_tcrt));

    gpio_config_t digital = {
        .pin_bit_mask = 1ULL << hardware->tcrt5000.digital_out,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&digital) != ESP_OK) return APP_STATUS_GPIO_ERROR;

    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = hardware->tcrt5000.adc_unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit, &s_tcrt.adc) != ESP_OK) {
        return APP_STATUS_ADC_ERROR;
    }
    adc_oneshot_chan_cfg_t channel = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_tcrt.adc, hardware->tcrt5000.adc_channel,
                                   &channel) != ESP_OK) {
        adc_oneshot_del_unit(s_tcrt.adc);
        memset(&s_tcrt, 0, sizeof(s_tcrt));
        return APP_STATUS_ADC_ERROR;
    }

    adc_cali_curve_fitting_config_t cal = {
        .unit_id = hardware->tcrt5000.adc_unit,
        .chan = hardware->tcrt5000.adc_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_tcrt.calibrated = adc_cali_create_scheme_curve_fitting(&cal,
                                                              &s_tcrt.calibration) == ESP_OK;
    s_tcrt.digital_pin = hardware->tcrt5000.digital_out;
    s_tcrt.adc_channel = hardware->tcrt5000.adc_channel;
    s_tcrt.active_low = device->tcrt5000.digital_active_low;
    s_tcrt.initialized = true;
    return APP_STATUS_OK;
}

tcrt5000_digital_result_t tcrt5000_read_digital(void)
{
    tcrt5000_digital_result_t result = {.status = APP_STATUS_NOT_INITIALIZED};
    if (!s_tcrt.initialized) return result;

    const int level = gpio_get_level(s_tcrt.digital_pin);
    if (level != 0 && level != 1) {
        result.status = APP_STATUS_GPIO_ERROR;
        return result;
    }
    result.raw_level = level;
    result.detected = s_tcrt.active_low ? level == 0 : level == 1;
    result.status = APP_STATUS_OK;
    return result;
}

tcrt5000_analog_result_t tcrt5000_read_analog(void)
{
    tcrt5000_analog_result_t result = {.status = APP_STATUS_NOT_INITIALIZED};
    if (!s_tcrt.initialized) return result;

    if (adc_oneshot_read(s_tcrt.adc, s_tcrt.adc_channel, &result.raw) != ESP_OK) {
        result.status = APP_STATUS_ADC_ERROR;
        return result;
    }
    if (s_tcrt.calibrated &&
        adc_cali_raw_to_voltage(s_tcrt.calibration, result.raw,
                                &result.millivolts) == ESP_OK) {
        result.millivolts_valid = true;
    }
    result.status = APP_STATUS_OK;
    return result;
}
