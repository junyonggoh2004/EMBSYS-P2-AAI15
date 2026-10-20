// main.c
// Entry point of the ESP32-S3 firmware on the course µT-Kernel 3 SMP port.
// The port starts µT-Kernel instead of FreeRTOS and calls usermain() from its
// initial task.
#include <tk/tkernel.h>
#include "esp_rom_sys.h"

EXPORT INT usermain(void)
{
    esp_rom_printf("[BOOT] esp32s3_mtk: micro T-Kernel 3 SMP\n");
    tk_slp_tsk(TMO_FEVR);   // usermain must not return: inittask treats it as shutdown
    return 0;
}
