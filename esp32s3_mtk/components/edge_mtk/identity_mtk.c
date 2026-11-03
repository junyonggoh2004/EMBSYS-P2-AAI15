/* Startup-only durable identity. Never called from SchedulerTask. No erase or
 * recovery-by-reset: failure disables real window events while D1 stays usable. */
#include <stdint.h>
#ifdef EDGE_IDENTITY_TEST
#include "identity_mtk_test.h"
#else
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#endif
#include "application/event_identity.h"
#include "hal/hal.h"

int identity_mtk_init(void)
{
    if (event_identity_ready()) return HAL_ERR_BUSY;
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK || nvs_flash_init() != ESP_OK) return HAL_ERR_IO;
    uint64_t node = 0;
    for (unsigned i = 0; i < sizeof mac; ++i) node = (node << 8) | mac[i];
    if (!node || (mac[0] & 1u)) return HAL_ERR_INVALID;
    nvs_handle_t h;
    if (nvs_open("edge_event", NVS_READWRITE, &h) != ESP_OK) return HAL_ERR_IO;
    uint32_t boot = 0;
    esp_err_t er = nvs_get_u32(h, "boot", &boot);
    if ((er != ESP_OK && er != ESP_ERR_NVS_NOT_FOUND) ||
        (er == ESP_OK && !boot) || boot == UINT32_MAX) {
        nvs_close(h); return HAL_ERR_IO;
    }
    er = nvs_set_u32(h, "boot", boot + 1);
    if (er == ESP_OK) er = nvs_commit(h);
    uint32_t verified = 0;
    if (er == ESP_OK) er = nvs_get_u32(h, "boot", &verified);
    nvs_close(h);
    if (er != ESP_OK || verified != boot + 1) return HAL_ERR_IO;
    return event_identity_install(node, verified) ? HAL_OK : HAL_ERR_INVALID;
}
