#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "identity_mtk_test.h"
/* Actual startup implementation, with only persistence/eFuse boundaries mocked.
 * Including the portable source lets this fixture simulate a fresh RAM boot. */
#include "../libraries/EdgeCore/src/application/event_identity.c"
#define EDGE_IDENTITY_TEST
#include "../esp32s3_mtk/components/edge_mtk/identity_mtk.c"
static uint32_t stored, pending;
static unsigned commits, closes;
static int failure;
esp_err_t esp_efuse_mac_get_default(uint8_t *mac)
{ const uint8_t value[]={2,1,2,3,4,5}; memcpy(mac,value,6); return failure==1?-1:ESP_OK; }
esp_err_t nvs_flash_init(void) { return failure==2?-1:ESP_OK; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h)
{ assert(!strcmp(name,"edge_event") && mode==NVS_READWRITE); *h=1; return failure==3?-1:ESP_OK; }
esp_err_t nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *value)
{
    assert(h==1 && !strcmp(key,"boot"));
    if(failure==4) return -1;
    *value=stored;
    return stored?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *key,uint32_t value)
{ assert(h==1 && !strcmp(key,"boot")); pending=value; return failure==5?-1:ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h)
{ assert(h==1); ++commits; if(failure==6) return -1; stored=pending; return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h==1); ++closes; }
static void ram_boot(void) { s_node=0; s_boot=s_sequence=0; }
int main(void)
{
    uint64_t node; uint32_t boot,sequence;
    for(failure=1;failure<=6;++failure) {
        ram_boot(); stored=0;
        assert(identity_mtk_init()!=HAL_OK && !event_identity_ready());
        assert(!event_identity_next(&node,&boot,&sequence));
    }
    failure=0; stored=0; ram_boot();
    assert(identity_mtk_init()==HAL_OK);
    assert(event_identity_next(&node,&boot,&sequence));
    assert(node==UINT64_C(0x020102030405) && boot==1 && sequence==1);
    assert(event_identity_next(&node,&boot,&sequence) && sequence==2);
    assert(identity_mtk_init()==HAL_ERR_BUSY);
    ram_boot(); assert(identity_mtk_init()==HAL_OK);
    assert(event_identity_next(&node,&boot,&sequence) && boot==2 && sequence==1);
    s_sequence=UINT32_MAX;
    assert(!event_identity_next(&node,&boot,&sequence)); // never wrap
    ram_boot(); stored=UINT32_MAX;
    assert(identity_mtk_init()!=HAL_OK && !event_identity_ready());
    assert(commits && closes);
    puts("event identity: PASS (actual NVS startup, commit failure, reboot identity and exhaustion)");
}
