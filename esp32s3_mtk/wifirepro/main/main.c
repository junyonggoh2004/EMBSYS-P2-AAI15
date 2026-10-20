// Minimal Wi-Fi reproduction on the course port's SMP profile (see
// ../CMakeLists.txt). Credentials come from esp32s3_mtk/main/wifi_credentials.h.
#include <tk/tkernel.h>
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "wifi_sta.h"
#include "wifi_credentials.h"

LOCAL void idle_task(INT s, void *e) { (void)s; (void)e; for (;;) asm volatile ("waiti 0" ::: "memory"); }

LOCAL void net_task(INT s, void *e)
{
    (void)s; (void)e;
    wifi_sta_start(WIFI_SSID, WIFI_PASS);
    for (int i = 0; ; i++) {
        tk_dly_tsk(5000);
        if (i % 2 == 0) {
            wifi_scan_config_t sc = { 0 };
            esp_err_t er = esp_wifi_scan_start(&sc, true);
            uint16_t n = 0;
            esp_wifi_scan_get_ap_num(&n);
            wifi_ap_record_t r[16];
            uint16_t g = 16;
            esp_wifi_scan_get_ap_records(&g, r);           // frees the scan results
            esp_rom_printf("[T %lld s] scan err=%d n=%d\n", esp_timer_get_time() / 1000000, (int)er, (int)n);
        }
        esp_rom_printf("[T %lld s] state=%d ip=%s rssi=%d attempts=%u\n", esp_timer_get_time() / 1000000, (int)wifi_sta_state(),
                       wifi_sta_ip(), wifi_sta_rssi(), wifi_sta_attempts());
    }
}

EXPORT INT usermain(void)
{
    T_CTSK c = { .tskatr = TA_HLNG | TA_RNG0 | TA_ASSPRC, .itskpri = CNF_MAX_TSKPRI, .stksz = 1024,
                 .task = (FP)idle_task, .assprc = TP_PRC1 };
    tk_sta_tsk(tk_cre_tsk(&c), 0);
    c.assprc = TP_PRC2;
    tk_sta_tsk(tk_cre_tsk(&c), 0);
    T_CTSK n = { .tskatr = TA_HLNG | TA_RNG0 | TA_ASSPRC | TA_FPU, .itskpri = 20, .stksz = 16384,
                 .task = (FP)net_task, .assprc = TP_PRC1 };
    tk_sta_tsk(tk_cre_tsk(&n), 0);
    tk_slp_tsk(TMO_FEVR);
    return 0;
}
