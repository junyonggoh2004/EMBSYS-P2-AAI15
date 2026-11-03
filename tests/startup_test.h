#include <stdint.h>
typedef int INT,ER,ID,esp_reset_reason_t;
typedef unsigned char UB;
typedef void (*FP)(INT,void *);
typedef struct { FP task; unsigned tskatr,assprc; int itskpri; unsigned stksz; } T_CTSK;
#define EXPORT
#define TA_HLNG 1u
#define TA_RNG0 2u
#define TA_ASSPRC 4u
#define TA_FPU 8u
#define TP_PRC1 1u
#define TK_MAX_CORE 2
#define E_OK 0
#define TMO_FEVR -1
ER console_init(void);
esp_reset_reason_t esp_reset_reason(void);
void runtime_set_network(const char *,const char *,const char *,unsigned,const char *);
ER runtime_start(void);
ID tk_cre_tsk(const T_CTSK *);
ER tk_sta_tsk(ID,INT);
ER tk_del_tsk(ID);
void tk_exd_tsk(void);
ER tk_slp_tsk(INT);
ID tk_get_prc(void);
void tm_printf(const UB *,...);
