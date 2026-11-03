#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "main.c"
static jmp_buf halt;
static T_CTSK created;
static int create_rc=5,start_rc,console_rc,identity_rc,runtime_rc;
static unsigned console_calls,identity_calls,runtime_calls,network_calls,deletes,errors,exits;
ER console_init(void) { console_calls++; return console_rc; }
esp_reset_reason_t esp_reset_reason(void) { return 0; }
void runtime_set_network(const char *a,const char *b,const char *c,unsigned d,const char *e)
{ (void)a;(void)b;(void)c;(void)d;(void)e;network_calls++; }
int identity_mtk_init(void) { identity_calls++;return identity_rc; }
ER runtime_start(void) { runtime_calls++; return runtime_rc; }
ID tk_cre_tsk(const T_CTSK *task) { created=*task;return create_rc; }
ER tk_sta_tsk(ID id,INT code) { assert(id==5 && code==0);return start_rc; }
ER tk_del_tsk(ID id) { assert(id==5);deletes++;return 0; }
void tk_exd_tsk(void) { exits++;longjmp(halt,2); }
ER tk_slp_tsk(INT timeout) { assert(timeout==TMO_FEVR);longjmp(halt,1);return 0; }
ID tk_get_prc(void) { return TP_PRC1; }
void tm_printf(const UB *fmt,...) { (void)fmt;errors++; }
static void initial(void) {
    unsigned before=console_calls+identity_calls+runtime_calls+network_calls;
    if (!setjmp(halt)) usermain();
    assert(before==console_calls+identity_calls+runtime_calls+network_calls);
}
static void worker(void) { if (!setjmp(halt)) created.task(0,NULL); }
int main(void) {
    initial();
    assert(created.stksz==16384 && created.assprc==TP_PRC1);
    assert((created.tskatr&(TA_ASSPRC|TA_FPU))==(TA_ASSPRC|TA_FPU));
    worker(); assert(console_calls==1 && identity_calls==1 && runtime_calls==1 && network_calls==1 && exits==1);
    identity_rc=-4;worker();assert(runtime_calls==2 && exits==2);
    console_rc=-1;worker();assert(identity_calls==2 && runtime_calls==2 && exits==3);
    start_rc=-2;initial();assert(deletes==1 && errors==1);
    create_rc=-3;initial();assert(deletes==1 && errors==2);
    puts("startup task: PASS (small initial task, 16 KiB pinned FPU worker, identity before runtime, cleanup and failure paths)");
}
