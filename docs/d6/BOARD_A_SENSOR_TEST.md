# Board A: test AHT, light and two sonars from PowerShell

**Current D6 fixture, 21 November 2026.** Use the [current wiring map](BOARD_A_SONAR_ONLY_WIRING.md) and [`config/board_a.cfg`](../../config/board_a.cfg). There is no radar profile or radar wire. This tests sensor readings, not automatic doorway counting. AHT, light and both sonars have produced valid individual readings; the sonars also have intermittent timeout/invalid readings or apparent background returns. A stable fixed-target result and complete four-sensor pass remain unverified.

For the combined MQTT run, the easiest PowerShell view of **actual live values** is `python tools/show_live_values.py --board A` from the repository root while the broker and Board A are running. It prints C, %RH, lux and valid cm readings continuously, and labels invalid sonar echoes. Use `--board B` to check the same frames after Board B receives them. See [usecase_commands.md](../commands/usecase_commands.md) for broker and board startup. The serial-console procedure below remains useful for isolated tests.

## 1. Start the Board A console

Connect **only Board A** to the laptop at the ESP32 socket printed `USB` (native USB). Start a fresh PowerShell:

```powershell
cd "$HOME\EMBSYS-P2-AAI15"
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py -m serial.tools.list_ports -v
```

Find the current **Espressif USB JTAG/serial debug unit**, VID `303A`. Its `COM` number can change; the CH343 socket printed `COM` is not the firmware command console on this fixture. Replace `COMx` below with the native port:

```powershell
& $py -m serial.tools.miniterm COMx 115200 --dtr 0 --rts 0 --eol CRLF
```

The terminal is now inside miniterm: type firmware commands **there**, one line at a time, and press Enter. The input may be invisible because local echo is off; `Ctrl+T`, then `Ctrl+E` toggles local echo. Type `HELP` and require a `Commands:` response. `Ctrl+]` quits miniterm and returns to PowerShell. If the board does not respond, do not assume the COM number or currently flashed firmware is correct.

## 2. Test each sensor separately

Each block below is entered **in miniterm**. `STOP` and `CLEAR` replace the volatile sensor setup without reflashing. Wait several seconds after `RUN`. Look for `[SENSOR]` lines and their JSON. A config acknowledgement alone is not a sensor pass. Do not change wires while USB is connected.

### AHT temperature and humidity

```text
STOP
CLEAR
BEGINCFG|name=AHT|proto=i2c|i2c.sensor=aht|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x38|i2c.baud=100000|ENDCFG
SHOW
RUN
```

Expect `AHT` status OK/error 0 and plausible temperature/humidity values that change gradually; note the actual values. Then type `STOP`.

### TSL2561 light

```text
STOP
CLEAR
BEGINCFG|name=LIGHT|proto=i2c|i2c.sensor=tsl2561|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x29|i2c.baud=100000|ENDCFG
SHOW
RUN
```

Expect `LIGHT` status OK/error 0 and a lux value that changes when shaded or illuminated. Then type `STOP`.

### Sonar A (direct signal leads GPIO10/11)

```text
STOP
CLEAR
BEGINCFG|name=SONAR_A|proto=gpio|gpio.mode=pulse|gpio.trig=10|gpio.echo=11|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG
SHOW
RUN
```

Hold a flat target about 30–50 cm in front of A. Expect changing, plausible `distance_cm` values and status OK/error 0. `TIMEOUT`/`error=-1` means no valid echo was captured, not zero distance. Then type `STOP`.

### Sonar B (direct signal leads GPIO6/7)

```text
STOP
CLEAR
BEGINCFG|name=SONAR_B|proto=gpio|gpio.mode=pulse|gpio.trig=6|gpio.echo=7|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG
SHOW
RUN
```

Hold a flat target still about 40 cm in front of B for 10 seconds, then move it to a measured second distance. Require plausible values that track the change, and note timeouts separately. If B times out repeatedly, check **VCC→B4 and GND→B7** against the current wiring guide with USB unplugged; the earlier rail-fed path timed out. Then type `STOP`.

## 3. Four-sensor run and sequential pair

After each sensor passes separately, type `STOP`, `CLEAR`, then paste the **four exact `BEGINCFG` lines from `config/board_a.cfg`** into miniterm, followed by `SHOW`, `RUN`, and `TASKS`. Check all four names and values; a `-6` means the pulse-capture resource was busy. The source now makes periodic sonar B wait for sonar A; if `-6` recurs, verify the board actually runs firmware built with the 19 November GPIO driver change. Do not claim success from the source tree alone.

For a one-shot pair, stop sampling first:

```text
STOP
PAIR SONAR_A SONAR_B A 5000
PAIR SONAR_A SONAR_B B 5000
```

Look for `[PAIR] state=complete outcome=0` **with valid widths for both sonars** in each order. The two 20,000 µs echo timeouts plus 5,000 µs guard fit the 50,000 µs pair budget. `PAIR` is a diagnostic; it does not implement a person-crossing interval or update occupancy. Save the actual console output and note which physical sonar faced which side of the doorway.

The existing `WINDOW` command still needs radar and is **not** used for this D6 sonar-only test. The [current use case](SONAR_ONLY_USECASE.md) lists the later crossing, MQTT, flash and LCD integration gates.
