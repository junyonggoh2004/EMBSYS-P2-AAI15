// console_mtk.c
// USB console of the ESP32-S3 on µT-Kernel 3 (D1 task 27).
//
// The Super Mini's only USB port is the chip's USB-Serial-JTAG, driven here at
// register level (hal/usb_serial_jtag_ll.h), as the course port does for its
// own drivers, rather than through IDF's FreeRTOS-based driver.
//
// Output: printf, vprintf, puts and putchar are wrapped at link time
// (-Wl,--wrap, see CMakeLists.txt), so every line the shared code and IDF
// print goes through here. Each call is formatted into one buffer and written
// under a µT-Kernel mutex with priority inheritance, so lines printed by tasks
// on the two cores never interleave. Before the kernel objects exist, and in
// any context that cannot wait (an interrupt handler, dispatch disabled), the
// mutex is skipped and the bytes are written directly. When the 64-byte
// hardware FIFO is full the writer spins briefly, then sleeps a tick at a
// time; a write that makes no progress for 50 ms means no host is reading,
// so it is dropped and later writes do not wait until the host reads again.
//
// Input: the USB receive interrupt is registered with tk_def_int. Its handler
// only moves bytes from the hardware FIFO into a 4 KB ring and sets an event
// flag; the reader (ControlTask) waits on that flag. The handler is the only
// writer of the ring head and the reader the only writer of the tail. A full
// ring drops the byte and counts it (the REPL accepts 512-byte lines, so this
// needs the reader to stall for a 4 KB paste).
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <tk/tkernel.h>
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/interrupts.h"

#include "hal/hal.h"
#include "edge_mtk.h"

#define CON_TX_STALL_US  50000
#define CON_TX_SPIN_US   300
#define CON_RX_RING      4096            // power of two
#define CON_FLG_RX       0x0001u

static ID s_mtx;                         // console mutex (TA_INHERIT)
static ID s_flg;                         // CON_FLG_RX: bytes arrived
static volatile bool s_host_gone;
static volatile bool s_rx_irq;           // receive interrupt installed

static uint8_t s_ring[CON_RX_RING];
static volatile uint32_t s_head;         // written by the interrupt handler only
static volatile uint32_t s_tail;         // written by the reader only
static volatile uint32_t s_rx_irqs, s_rx_dropped;

// ---------- output ----------

static void write_fifo(const char *p, size_t n, bool can_sleep)
{
    uint64_t last = esp_timer_get_time();
    while (n) {
        uint32_t w = usb_serial_jtag_ll_write_txfifo((const uint8_t *)p, (uint32_t)n);
        if (w) {
            p += w;
            n -= w;
            usb_serial_jtag_ll_txfifo_flush();
            s_host_gone = false;
            last = esp_timer_get_time();
            continue;
        }
        uint64_t stalled = esp_timer_get_time() - last;
        if (s_host_gone || stalled > CON_TX_STALL_US) {
            s_host_gone = true;           // nobody is reading: drop the rest
            return;
        }
        if (can_sleep && stalled > CON_TX_SPIN_US) tk_dly_tsk(1);
    }
}

static bool con_lock(void)
{
    if (s_mtx <= 0) return false;          // kernel objects not created yet
    return tk_loc_mtx(s_mtx, 200) == E_OK; // E_CTX in a handler or with dispatch disabled
}

void con_write(const char *p, size_t n)
{
    bool locked = con_lock();
    write_fifo(p, n, locked);
    if (locked) tk_unl_mtx(s_mtx);
}

// Wrapped C library output. The large buffer is only used under the mutex;
// the small one only before the kernel starts or where waiting is impossible.
int __wrap_vprintf(const char *fmt, va_list ap)
{
    static char locked_buf[2048];
    static char raw_buf[256];
    bool locked = con_lock();
    char *buf = locked ? locked_buf : raw_buf;
    size_t cap = locked ? sizeof locked_buf : sizeof raw_buf;
    int n = vsnprintf(buf, cap, fmt, ap);
    if (n > 0) write_fifo(buf, (size_t)n < cap ? (size_t)n : cap - 1, locked);
    if (locked) tk_unl_mtx(s_mtx);
    return n;
}

int __wrap_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = __wrap_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int __wrap_puts(const char *s)
{
    bool locked = con_lock();
    size_t n = strlen(s);
    write_fifo(s, n, locked);
    write_fifo("\n", 1, locked);
    if (locked) tk_unl_mtx(s_mtx);
    return (int)n + 1;
}

int __wrap_putchar(int c)
{
    char ch = (char)c;
    con_write(&ch, 1);
    return c & 0xFF;
}

// ---------- input ----------

// Task-independent context: no waiting service calls, no application data.
static void usb_rx_handler(UINT intno)
{
    (void)intno;
    s_rx_irqs++;
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    uint8_t b;
    while (usb_serial_jtag_ll_rxfifo_data_available() && usb_serial_jtag_ll_read_rxfifo(&b, 1) == 1) {
        uint32_t head = s_head;
        if (head - __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE) >= CON_RX_RING) {
            s_rx_dropped++;
            continue;
        }
        s_ring[head & (CON_RX_RING - 1)] = b;
        __atomic_store_n(&s_head, head + 1, __ATOMIC_RELEASE);
    }
    tk_set_flg(s_flg, CON_FLG_RX);
}

ER console_init(void)
{
    T_CMTX cmtx = { .exinf = NULL, .mtxatr = TA_TFIFO | TA_INHERIT, .ceilpri = 0 };
    T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TFIFO | TA_WSGL, .iflgptn = 0 };
    ID mtx = tk_cre_mtx(&cmtx);
    if (mtx < E_OK) return (ER)mtx;
    ID flg = tk_cre_flg(&cflg);
    if (flg < E_OK) return (ER)flg;
    s_flg = flg;
    s_mtx = mtx;
    return E_OK;
}

// Registers the receive interrupt on the calling core (task context, once).
ER console_start_rx(void)
{
    T_DINT dint = { .intatr = TA_HLNG, .inthdr = (FP)usb_rx_handler };
    ER er = tk_def_int(ETS_USB_SERIAL_JTAG_INTR_SOURCE, &dint);
    if (er != E_OK) return er;
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    s_rx_irq = true;
    EnableInt(ETS_USB_SERIAL_JTAG_INTR_SOURCE, 1);
    return E_OK;
}

void console_stats(uint32_t *irqs, uint32_t *dropped)
{
    *irqs = s_rx_irqs;
    *dropped = s_rx_dropped;
}

static int ring_pop(void)
{
    uint32_t tail = s_tail;
    if (__atomic_load_n(&s_head, __ATOMIC_ACQUIRE) == tail) return -1;
    int ch = s_ring[tail & (CON_RX_RING - 1)];
    __atomic_store_n(&s_tail, tail + 1, __ATOMIC_RELEASE);
    return ch;
}

int hal_console_getc(uint32_t timeout_us)
{
    uint64_t end = esp_timer_get_time() + timeout_us;
    if (!s_rx_irq) {                      // before console_start_rx(): poll the FIFO
        for (;;) {
            uint8_t b;
            if (usb_serial_jtag_ll_rxfifo_data_available() && usb_serial_jtag_ll_read_rxfifo(&b, 1) == 1)
                return b;
            if (esp_timer_get_time() >= end) return HAL_ERR_TIMEOUT;
            if (tk_dly_tsk(1) != E_OK) esp_rom_delay_us(100);
        }
    }
    for (;;) {
        int ch = ring_pop();
        if (ch >= 0) return ch;
        uint64_t now = esp_timer_get_time();
        if (now >= end) return HAL_ERR_TIMEOUT;
        UINT pat;
        // The handler stores bytes before setting the flag, and the ring is
        // checked again after every wake-up, so a byte can never be missed.
        (void)tk_wai_flg(s_flg, CON_FLG_RX, TWF_ORW | TWF_BITCLR, &pat,
                         (TMO)((end - now + 999) / 1000));
    }
}
