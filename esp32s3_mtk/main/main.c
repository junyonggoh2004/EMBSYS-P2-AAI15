// main.c
// Entry point of the ESP32-S3 firmware on the course µT-Kernel 3 SMP port.
// The port starts µT-Kernel instead of FreeRTOS and calls usermain() from its
// initial task.
//
// Task 26 bring-up: one idle task per processor, then one task pinned to each
// processor that reports where it runs, to show both dispatchers schedule.
#include <tk/tkernel.h>
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

// Lowest-priority task, one per processor: when every other task on that
// processor is blocked the dispatcher runs this one, which waits for the next
// interrupt.
LOCAL void idle_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;
    for (;;) asm volatile ("waiti 0" ::: "memory");
}

LOCAL void probe_task(INT stacd, void *exinf)
{
    (void)stacd;
    int want = (int)(intptr_t)exinf;
    for (int i = 0; i < 5; i++) {
        esp_rom_printf("[BRINGUP] task pinned to core %d: tick %d running on core %d\n",
                       want, i, esp_cpu_get_core_id());
        tk_dly_tsk(500 + 250 * want);   // different periods so the lines do not collide
    }
    tk_ext_tsk();
}

LOCAL ER start_pinned(FP entry, PRI pri, SZ stack, int core, void *arg)
{
    T_CTSK c = {
        .exinf   = arg,
        .tskatr  = TA_HLNG | TA_RNG0 | TA_ASSPRC,
        .task    = entry,
        .itskpri = pri,
        .stksz   = stack,
        .assprc  = core == 0 ? TP_PRC1 : TP_PRC2,
    };
    ID id = tk_cre_tsk(&c);
    if (id < E_OK) return (ER)id;
    return tk_sta_tsk(id, 0);
}

EXPORT INT usermain(void)
{
#ifdef CONFIG_MTK_SMP
    esp_rom_printf("[BOOT] esp32s3_mtk: micro T-Kernel 3 SMP, %d processors, usermain on core %d\n",
                   (int)TK_MAX_CORE, esp_cpu_get_core_id());
#else
    esp_rom_printf("[BOOT] esp32s3_mtk: micro T-Kernel 3 single core\n");
#endif
    ER er = start_pinned((FP)idle_task, CNF_MAX_TSKPRI, 1024, 0, NULL);
    if (er == E_OK) er = start_pinned((FP)idle_task, CNF_MAX_TSKPRI, 1024, 1, NULL);
    if (er == E_OK) er = start_pinned((FP)probe_task, 10, 3072, 0, (void *)0);
    if (er == E_OK) er = start_pinned((FP)probe_task, 10, 3072, 1, (void *)1);
    esp_rom_printf("[BOOT] tasks %s (er=%d)\n", er == E_OK ? "started" : "FAILED", (int)er);

    tk_slp_tsk(TMO_FEVR);   // usermain must not return: inittask treats it as shutdown
    return 0;
}
