"""Audit the actual configurable image and compiler inputs after linking."""
from pathlib import Path
import json
import re
import sys

from elftools.elf.elffile import ELFFile

root=Path(sys.argv[1]); build=Path(sys.argv[2]) if len(sys.argv)>2 else root/'build'
config=(build/'config/sdkconfig.h').read_text()
for key in ['CONFIG_MTK_SMP','CONFIG_SPIRAM','CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG']:
    assert f'#define {key} 1' in config, f'{key} disabled'
assert '#define CONFIG_FREERTOS_UNICORE 1' not in config
commands=json.loads((build/'compile_commands.json').read_text())
sources=[str(x['file']).replace('\\','/') for x in commands]
for suffix in ['/FreeRTOS-Kernel/tasks.c','/FreeRTOS-Kernel/queue.c','/FreeRTOS-Kernel/timers.c','/FreeRTOS-Kernel/event_groups.c','/portable/xtensa/portasm.S']:
    assert not any(s.endswith(suffix) for s in sources), f'Old scheduler source compiled: {suffix}'
for suffix in ['/application/scheduler.c','/bus/i2c_profiles.c','/application/sensor_measurement.c','/edge_mtk/runtime_mtk.c','/edge_mtk/sensor_ml.c']:
    assert any(s.endswith(suffix) for s in sources), f'Missing framework source: {suffix}'
with (build/'esp32s3_mtk.elf').open('rb') as image:
    table = ELFFile(image).get_section_by_name('.symtab')
    assert table is not None, 'Missing firmware symbol table'
    defined = {sym.name: sym.entry for sym in table.iter_symbols()
               if sym.entry['st_shndx'] != 'SHN_UNDEF'}
symbols = set(defined)
sink = defined.get('sensor_measurement_notify')
assert sink is not None and sink['st_info']['bind'] == 'STB_GLOBAL', 'ML sample sink is not strongly linked'
for symbol in ['__wrap_esp_startup_start_app','knl_dispatch','knl_smp_core1_entry','tk_cre_tsk',
               'tk_sta_tsk','tk_cre_mbf','runtime_start','hal_gpio_capture_start',
               '__wrap_esp_nn_fc_s16_esp32s3','esp_nn_fc_s16_esp32s3',
               'i2c_profile_poll','sensor_ml_command','sensor_ml_poll','sensor_measurement_value','sensor_measurement_notify','sensor_measurement_json','__wrap_printf']:
    assert symbol in symbols, f'Missing linked symbol: {symbol}'
for symbol in ['vTaskStartScheduler','xPortStartScheduler','xTaskIncrementTick','vTaskSwitchContext',
               'prvIdleTask','prvAddNewTaskToReadyList','pxCurrentTCBs','pxCurrentTCB',
               'xPortSysTickHandler','vPortSetupTimer','xQueueGenericSend','xTaskCreatePinnedToCore',
               'main_task','esp_startup_start_app']:
    assert symbol not in symbols, f'Original RTOS code linked: {symbol}'
for source in [*(root/'main').glob('*.[ch]'),*(root/'components/edge_mtk').glob('*.[ch]'),
               *(root/'components/edge_mtk').glob('*.cpp')]:
    assert not re.search(r'#\s*include\s*[<"](?:Arduino|Wire|freertos/)',source.read_text()), source
print('PASS: main configurable framework + sensor profiles linked; micro T-Kernel SMP startup/dispatcher present; original FreeRTOS scheduler excluded; PSRAM and native USB enabled.')
