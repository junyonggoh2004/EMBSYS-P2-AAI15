/* Validate rendered blocks with the real portable config/timing parsers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bus/bus_common.h"

bool hal_gpio_valid(int pin) { return pin >= 0 && pin < 19; }

int main(void)
{
    char line[512];
    app_cfg_t cfg = {0};
    unsigned count = 0;
    bool active = false;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strcmp(line, "BEGINCFG")) {
            assert(!active);
            memset(&cfg, 0, sizeof cfg);
            cfg.freq_hz = 1;
            cfg.timing.interval_ms = 1000;
            cfg.timing.enabled = true;
            cfg_parse_begin();
            active = true;
        } else if (!strcmp(line, "ENDCFG")) {
            assert(active);
            cfg_parse_end(&cfg);
            assert(!cfg.cfg_error[0]);
            if (cfg.proto == proto_i2c) {
                assert(cfg.i2c_sda == 1 && cfg.i2c_scl == 2 && cfg.i2c_baud == 100000);
                assert(cfg.timing.interval_ms == 2000 && cfg.timing.timeout_ms == 600);
                assert(cfg.i2c_sensor == I2C_SENSOR_AHT || cfg.i2c_sensor == I2C_SENSOR_TSL2561);
                if (!strcmp(cfg.name, "AHT")) assert(cfg.i2c_addr == 0x38);
                else assert(cfg.i2c_addr == 0x29 || cfg.i2c_addr == 0x39 || cfg.i2c_addr == 0x49);
            } else {
                assert(cfg.proto == proto_mqtt && cfg.mode == sample_mode_stream);
                assert(cfg.remote_node[0] && cfg.remote_source[0]);
                assert(cfg.remote_profile == (strstr(cfg.name, "LIGHT") ? SENSOR_PROFILE_TSL2561
                                                                       : SENSOR_PROFILE_AHT));
            }
            printf("%s:%d:%s:%s:%u\n", cfg.name, (int)cfg.proto,
                   cfg.remote_node, cfg.remote_source, (unsigned)cfg.timing.interval_ms);
            active = false;
            ++count;
        } else {
            assert(active);
            cfg_parse_line(line, &cfg);
        }
    }
    assert(!active && count > 0);
    return 0;
}
