# Board A N16R8: serial bring-up, one sensor at a time

Updated 19 November 2026. Use this with the [65-row Board A layout](BOARD_A_SPACED_65_ROW_LAYOUT.md)
and [physical test record](BOARD_A_STAGE1_BRINGUP_20261118.md). The layout
assigns different GPIOs to I2C, both sonars and radar. Its power rails and
full combined fixture are not yet electrically validated. No firmware or
wiring change is implied by these serial commands.

## Find the command console from a fresh PowerShell

Connect Board A only. The ESP32 firmware command console uses the socket
printed `USB` (native USB-Serial-JTAG), not the CH343 bridge socket printed
`COM` on this N16R8 board. Port numbers change. COM9 was observed as CH343
`VID:PID=1A86:55D3`; opening it did not establish REPL access. Find the
native port with the ESPressif Python environment already installed locally:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py -m serial.tools.list_ports -v
& $py -m serial.tools.miniterm COMx 115200 --dtr 0 --rts 0 --eol CRLF
```

Replace `COMx` with the current native USB port (normally described as USB
JTAG/serial debug unit, Espressif VID `303A`). In `miniterm`, type `HELP` and
Enter; require a `Commands:` response before loading a profile. Local echo is
off by default, so typed text may be invisible. `Ctrl+T`, then `Ctrl+E`
toggles local echo. `Ctrl+]` quits. If only CH343 appears, resolve native USB
enumeration before issuing sensor commands. Do not infer a working console
from `miniterm`'s banner alone.

In the *open serial monitor*, check the volatile configuration with `SHOW`
and task state with `TASKS`. Enter each line below separately, pressing Enter.
`STOP`/`CLEAR` remove the prior volatile sensor/rule configuration. An ESP
reset or USB power cycle also requires reloading the desired profile.

## Stage 1: Sonar A alone

The complete layout uses ESP GPIO4 -> A8 / E8 -> Sonar A TRIG, and GPIO5 ->
A9 / E9 -> Sonar A ECHO. Sonar A VCC is C4 and GND is C7. Temporary D4/D7
power positions used when the light module was absent are the same electrical
rows, but D4/D7 belong to light in the complete fixture.

```text
STOP
CLEAR
BEGINCFG|name=SONAR_A|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=0.5|timing.timeout_ms=100|gpio.trig=4|gpio.echo=5|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|ENDCFG
SHOW
RUN
```

Place a flat target 30-50 cm ahead, record several results, then enter
`STOP`. `CFG: ok` confirms parser acceptance, not a working Echo. The most
recent A-only capture still returned `TIMEOUT` / `error=-1`. Earlier physical
swaps established that the A module and GPIO4/5 path can return valid distances
with B's proven four-lead set; A's original leads or contacts are suspect.
Replace the complete A lead set with known-good leads as one controlled repair,
keeping the same GPIOs and rows, then reload this profile after power cycling.
Do not claim A is fixed until fresh `OK` distance readings are captured.

## Stage 2: Sonar B alone

Only after A's result is recorded, replace the profile with GPIO6 -> A28 / E28
-> B TRIG and GPIO7 -> A29 / E29 -> B ECHO:

```text
STOP
CLEAR
BEGINCFG|name=SONAR_B|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=0.5|timing.timeout_ms=100|gpio.trig=6|gpio.echo=7|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|ENDCFG
SHOW
RUN
```

Record readings with the same flat target and enter `STOP`. The 65-row
diagram's rail-fed B power previously gave invalid short pulses, whereas
direct row-4/row-7 power produced valid distances. The rail-fed full fixture
still needs validation; software configuration cannot establish rail continuity.

## Stage 3: sequential pair diagnostic

After **both** sonars pass independently, enter `STOP`, `CLEAR`, both
`BEGINCFG` lines above, `SHOW`, and then (while stopped):

```text
PAIR SONAR_A SONAR_B A 5000
```

The two 20,000 us timeouts plus 5,000 us guard fit the 50,000 us pair budget.
Inspect both `[PAIR]` sensor lines. Do not start both as ordinary periodic
`RUN` sensors at the same instant: the single pulse capture resource can give
the second sensor `-6` (`HAL_ERR_BUSY`). That busy result is distinct from
Sonar A's isolated `-1` timeout.

## Remaining inputs

The existing [three-sensor preset](../../config/three_sensors.cfg) covers AHT,
light and Sonar A only; it does not configure B or radar. AHT and TSL2561
were previously verified in isolated N16R8 trials on shared GPIO1 SDA/GPIO2
SCL, at addresses `0x38` and `0x29`. Radar uses the proposed GPIO17 receive
and GPIO18 transmit mapping (LD2450 TX -> GPIO17); its 256000-baud profile
was accepted but received no raw bytes. Radar power at its connector and the
TX electrical path remain unmeasured. Test radar alone after the sonar stages;
do not claim a full D6 doorway run from accepted configuration alone.
