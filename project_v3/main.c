#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "bus/bus_common.h"
#include "application/scheduler.h"
#include "mqtt/mqtt_telemetry.h"
#include "mqtt/mqtt_model_transfer.h"
#include "rule_engine/rules.h"
#include "inference_engine/inference_manager.h"


#include "wifi_credentials.h"


// From control_glue.c
extern void control_bind_flags(bool *running, bool *cfg_ready);
extern bool control_mqtt_start(const char *broker, uint16_t port, const char *client_id, const char *node_id);

int main(void) {
    stdio_init_all();

    // Wi-Fi
    if (cyw43_arch_init()) { printf("WiFi init failed\n"); return -1; }
    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("WiFi connect failed\n");
    } else {
        printf("WiFi connected\n");
    }

    // Empty scheduler; REPL seeds defaults only for new blocks
    app_cfg_t dummy; repl_init_defaults(&dummy);
    scheduler_reset();

    bool running = false, cfg_ready = false;
    control_bind_flags(&running, &cfg_ready);

    // Initialise inference subsystem
    model_transfer_init();
    inference_mgr_init();
    
    // Load the base autoencoder model for testing (optional logic, bypasses Flash map)
    inference_mgr_load_base_model();

    // Choose a node/client id (e.g., derive from MAC tail if you like)
    const char *node_id   = "pico-001";
    const char *client_id = "pico-001";

    control_mqtt_start(MQTT_HOST, MQTT_PORT, client_id, node_id);

    absolute_time_t next_hb = make_timeout_time_ms(10000);
    while (true) {


        // REPL still works in parallel
        repl_poll(&dummy, &running, &cfg_ready);

        // Run all active sensors
        scheduler_run_all(running);

        // Pump MQTT (reconnects, callbacks)
        mqtt_poll(); 

        // Inference engine: auto-load models, run deferred ops
        model_transfer_poll();
        inference_mgr_poll();

        // Heartbeat
        if (absolute_time_diff_us(get_absolute_time(), next_hb) <= 0) {
            static uint32_t up = 0;
            mqtt_publish_heartbeat(++up);
            next_hb = make_timeout_time_ms(10000);
        }

        // Keep USB, MQTT, and the scheduler responsive to configured rates.
        sleep_ms(10);
    }
    return 0;
}


// BEGINCFG|name=GY511_ACC|proto=i2c|mode=poll|freq_hz=5|i2c.sda=4|i2c.scl=5|i2c.addr=0x1E|i2c.pre=0x00 0x10 0x02 0x00|i2c.post_delay_ms=2|i2c.reg=0x03|i2c.reg_size=1|i2c.read_len=6|i2c.restart=1|ENDCFG
// BEGINRULE|name=GY511_MOTION|source=GY511_ACC|calc=mx=s16be(0)|calc=mz=s16be(2)|calc=my=s16be(4)|calc=dx=mx-mx_prev|calc=dy=my-my_prev|calc=dz=mz-mz_prev|calc=motion=sqrt(dx*dx+dy*dy+dz*dz)|when=motion>120|action=log:motion detected|ENDRULE
