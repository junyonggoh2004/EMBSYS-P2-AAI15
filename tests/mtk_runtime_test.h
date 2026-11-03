#ifndef MTK_RUNTIME_TEST_H
#define MTK_RUNTIME_TEST_H

/* Test doubles only. These declarations cannot establish SDK ABI compatibility
 * or kernel scheduling/interrupt behavior; build/image/board gates remain. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int ID, ER, INT, PRI, TMO;
typedef size_t SZ;
typedef unsigned ATR, UW, UINT;
typedef void (*FP)(void);
#define E_OK 0
#define E_TMOUT (-50)
#define E_NOMEM (-33)
#define TMO_POL 0
#define TMO_FEVR (-1)
#define TA_TFIFO 1u
#define TA_USERBUF 2u
#define TA_WMUL 4u
#define TA_HLNG 8u
#define TA_RNG0 16u
#define TA_ASSPRC 32u
#define TA_FPU 64u
#define TA_STA 128u
#define TP_PRC1 1u
#define TP_PRC2 2u
#define CNF_MAX_TSKPRI 32
#define TK_MAX_CORE 2

typedef struct { void *exinf; ATR mbfatr; SZ bufsz; INT maxmsz; void *bufptr; } T_CMBF;
typedef struct { SZ frbufsz; } T_RMBF;
typedef struct { void *exinf; ATR flgatr; UINT iflgptn; } T_CFLG;
typedef struct { UINT flgptn; } T_RFLG;
typedef struct {
    void *exinf; ATR tskatr; FP task; PRI itskpri; SZ stksz; void *bufptr; UW assprc;
} T_CTSK;
typedef struct { PRI tskpri; } T_RTSK;
typedef struct { void *exinf; ATR cycatr; FP cychdr; unsigned cyctim, cycphs; } T_CCYC;

ID tk_cre_mbf(const T_CMBF *config);
ER tk_snd_mbf(ID id, void *message, INT bytes, TMO timeout);
INT tk_rcv_mbf(ID id, void *message, TMO timeout);
ER tk_ref_mbf(ID id, T_RMBF *state);
ID tk_cre_flg(const T_CFLG *config);
ER tk_ref_flg(ID id, T_RFLG *state);
ID tk_cre_tsk(const T_CTSK *config);
ER tk_sta_tsk(ID id, INT start);
ER tk_ref_tsk(ID id, T_RTSK *state);
ID tk_cre_cyc(const T_CCYC *config);
ID tk_get_prc(void);
ER tk_dly_tsk(TMO delay);
ER tk_slp_tsk(TMO timeout);
ER tk_wup_tsk(ID id);

typedef struct { int unused; } wdt_hal_context_t;
#define PERIPH_TIMG0_MODULE 0
#define PERIPH_RCC_ACQUIRE_ATOMIC(module, ref_count) \
    for (int ref_count = 0; ref_count == 0; ref_count = 1)
#define WDT_MWDT0 0
#define MWDT_LL_DEFAULT_CLK_PRESCALER 0
#define WDT_STAGE0 0
#define WDT_STAGE_ACTION_RESET_SYSTEM 0
#define MALLOC_CAP_INTERNAL 1u
void timg_ll_enable_bus_clock(int group, bool enabled);
void timg_ll_reset_register(int group);
void wdt_hal_init(wdt_hal_context_t *context, int timer, int prescaler, bool enabled);
void wdt_hal_write_protect_disable(wdt_hal_context_t *context);
void wdt_hal_write_protect_enable(wdt_hal_context_t *context);
void wdt_hal_config_stage(wdt_hal_context_t *context, int stage, int ticks, int action);
void wdt_hal_enable(wdt_hal_context_t *context);
void wdt_hal_feed(wdt_hal_context_t *context);
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_minimum_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);

#endif
