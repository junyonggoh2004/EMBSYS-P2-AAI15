/* Route T-Monitor through native USB on the generic Super Mini.
 * Linker wrappers keep the external kernel source unchanged.
 */
#include <tk/tkernel.h>
#include "esp_rom_sys.h"
#include "esp_rom_serial_output.h"
#include "esp_rom_caps.h"
#include "hal/usb_serial_jtag_ll.h"
void __wrap_tm_snd_dat(const UB *buffer,INT size) {
    for (INT i=0;i<size;++i) {
        int budget=2000;
        while (esp_rom_output_tx_one_char(buffer[i])!=0) {
            if (--budget==0) return;
            esp_rom_output_flush_tx(ESP_ROM_USB_SERIAL_DEVICE_NUM);
            esp_rom_delay_us(1);
        }
    }
    esp_rom_output_flush_tx(ESP_ROM_USB_SERIAL_DEVICE_NUM);
}
void __wrap_tm_rcv_dat(UB *buffer,INT size) {
    for (INT i=0;i<size;++i) while (esp_rom_output_rx_one_char(&buffer[i])!=0) { }
}
void __wrap_tm_com_init(void) {
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SOF);
    for (int ms=0;ms<2000;++ms) {
        if (usb_serial_jtag_ll_get_intraw_mask() & USB_SERIAL_JTAG_INTR_SOF) {
            esp_rom_delay_us(100000); return;
        }
        esp_rom_delay_us(1000);
    }
}
