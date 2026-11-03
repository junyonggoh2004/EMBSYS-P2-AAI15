"""Check the real linked image/configuration, not a mock kernel."""
from pathlib import Path
import json,re,sys
root=Path(sys.argv[1]); build=root/'build_mtk'
config=(build/'config/sdkconfig.h').read_text()
assert '#define CONFIG_MTK_SMP 1' in config, 'SMP disabled'
assert '#define CONFIG_FREERTOS_UNICORE 1' not in config, 'IDF single-core profile selected'
commands=json.loads((build/'compile_commands.json').read_text())
sources=[str(x['file']).replace('\\','/') for x in commands]
for suffix in ['/FreeRTOS-Kernel/tasks.c','/FreeRTOS-Kernel/queue.c','/FreeRTOS-Kernel/timers.c','/FreeRTOS-Kernel/event_groups.c','/portable/xtensa/portasm.S']:
    assert not any(s.endswith(suffix) for s in sources), f'Old kernel source compiled: {suffix}'
mapfile=next(build.glob('*.map')); symbols=mapfile.read_text(errors='replace')
for required in ['__wrap_esp_startup_start_app','knl_main','knl_dispatch',
                 'knl_smp_core1_entry','knl_timer_handler','tk_cre_tsk',
                 'tk_cre_mtx','tk_loc_mtx','tk_get_prc',
                 '__wrap_tm_snd_dat','__wrap_tm_com_init']:
    assert re.search(r'^\s+0x[0-9a-fA-F]+\s+'+required+r'\s*$',symbols,re.M), f'Missing linked kernel/console symbol: {required}'
for forbidden in ['vTaskStartScheduler','xPortStartScheduler']:
    # Only actual linked addresses count; discarded/debug mentions are not executable definitions.
    assert not re.search(r'^\s+0x[0-9a-fA-F]+\s+'+forbidden+r'\s*$',symbols,re.M), f'Old scheduler linked: {forbidden}'
for source in (root/'main').glob('*.[ch]'):
    text=source.read_text()
    assert not re.search(r'#\s*include\s*[<"](?:Arduino|Wire|freertos/)',text), f'Forbidden application include: {source.name}'
print('PASS: real micro T-Kernel startup and SMP configuration; old FreeRTOS scheduler sources excluded; application has no Arduino/FreeRTOS includes.')
