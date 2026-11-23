# D6 MQTT commands — Board A telemetry and Board B bridge

This is the **tested Board A → laptop Mosquitto** procedure for the ESP32-S3
N16R8 node `s3-001`. Board A uses the four radar-free profiles in
[`config/board_a.cfg`](config/board_a.cfg): AHT, light, Sonar A and Sonar B.
Run these commands from the merged `main` checkout. For a shorter three-board
sequence, use [`usecase_commands.md`](usecase_commands.md). For Board B's live `sine-float` default, optional model switches and replay-only `people-counter`, use [`ML_commands.md`](ML_commands.md). To see decoded
readings continuously, use [`tools/show_live_values.py`](tools/show_live_values.py)
as described below; the raw MQTT line subscriber remains useful for checking
topics and transport.
It proves sensor frames reach the broker and a laptop subscriber. It does not
prove accurate sonar distance, doorway occupancy, Board B processing, SPI
flash writes or a live Board C display. The combined A → B commands below
have now produced all four Board B sensor aliases; accuracy and the doorway
workflow remain separate tests.

## Board A only — before each session

- Connect the laptop and Board A to the same private **2.4 GHz Wi-Fi**.
- Power Board A from USB. Its firmware must be built with the same Wi-Fi name,
  password and laptop broker IP in the ignored local
  `esp32s3_mtk/main/wifi_credentials.h`. Do not commit that private file.
- For the combined test below, also power Board B from USB and confirm its
  `s3-002` firmware uses the same Wi-Fi and broker address.
- Use a **normal PowerShell** to check the laptop's current Wi-Fi address:

  ```powershell
  cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
  $brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
  $brokerIp
  ```

  If the address differs from the address compiled into Board A, update the ignored
  credentials file and rebuild/flash Board A before proceeding. See
  [When Wi-Fi or the broker address changes](#when-wi-fi-or-the-broker-address-changes).

## Start: Window 1 — Administrator PowerShell (broker)

Check whether a broker is already listening. **Do not start a second copy**
on port 1883. The normal Windows Mosquitto service may listen on localhost
only; the temporary D6 listener must accept the ESP32 on the Wi-Fi network.

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
Get-Service mosquitto
netstat -ano -p tcp | Select-String ':1883'
```

If the temporary broker is already listening on `0.0.0.0:1883`, keep its
window open and skip the next block. Otherwise, stop the default service and
start the temporary broker **in this Administrator window**:

```powershell
Stop-Service -Name mosquitto -ErrorAction SilentlyContinue
& "$env:ProgramFiles\mosquitto\mosquitto.exe" -c '.\tools\mqtt-test.conf' -v
```

Keep the window open at `mosquitto version ... running`. This command stays
in the foreground, so enter later commands in other PowerShell windows. The
temporary [`mqtt-test.conf`](tools/mqtt-test.conf) opens port 1883 to the
private network and permits anonymous test clients. If Windows Firewall
prompts, allow port 1883 on **Private** networks for this test.

## Watch: Window 2 — normal PowerShell (subscriber)

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h $brokerIp -t 'pico/s3-001/#' -v
```

Keep this window open. Once Board A connects, expect
`pico/s3-001/presence`, `pico/s3-001/heartbeat`, then sensor topics after
`RUN`. The `.../sensor/<name>/line` payload contains **hex bytes**. Open a
separate **normal PowerShell** window for actual live values:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py .\tools\show_live_values.py --board A
```

This prints AHT temperature in C and humidity in %RH, light in lux, and valid
sonar distances in cm. It reports errors and invalid echoes rather than
displaying them as valid distances. Use `--board B` in another window for B's
received copies. The viewer connects to the broker on laptop localhost by
default, while the boards continue using the Wi-Fi address in their firmware.
Press **Ctrl+C** to stop the viewer. The optional serial console below also
prints decoded JSON.

## Run: Window 3 — normal PowerShell (send Board A commands)

Wait until Board A is connected. Then run these lines in order:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

& $pub -h $brokerIp -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -t 'pico/s3-001/config' -f '.\config\board_a.cfg'
Start-Sleep -Milliseconds 800
& $pub -h $brokerIp -t 'pico/s3-001/cmd' -m 'RUN'
```

Window 2 should show `pico/s3-001/sensor/AHT/line`, `LIGHT/line`,
`SONAR_A/line` and `SONAR_B/line`. A publish command returning to the prompt
only proves the **laptop sent it to Mosquitto**. If Board A was offline,
these non-retained commands missed it; wait for a fresh heartbeat or presence
and send the block again. A configuration acknowledgement alone is not a
valid sensor reading.

## Test Board A sensors separately — normal PowerShell

Keep the broker running and Board A connected. Use a **separate normal
PowerShell window** for these commands; a `mosquitto_sub` window stays busy
until you press `Ctrl+C`. Run **one sensor block at a time**. Each block sends
`STOP` and `CLEAR`, so it replaces Board A's previous runtime profiles with
only that sensor. It does not change wiring or flash firmware. Board B can
forward the frames if its four MQTT bridge profiles are already configured.

To watch only the selected source's raw frame, use a separate normal PowerShell window. For numeric values, use the live viewer above:

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h $brokerIp -t 'pico/s3-001/sensor/+/line' -v
```

### AHT temperature and humidity

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -m 'BEGINCFG|name=AHT|proto=i2c|i2c.sensor=aht|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x38|i2c.baud=100000|ENDCFG'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
```

Expect `pico/s3-001/sensor/AHT/line`. The decoded temperature and humidity
are in Board A's serial `[SENSOR]` JSON. On 21 November, AHT first returned
`error=-4` (I²C device did not respond), then returned valid readings after a
`STOP`/`RUN` cycle without rewiring. Treat another `-4` as a current failure,
not proof that the earlier reading was stable.

### TSL2561 light

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -m 'BEGINCFG|name=LIGHT|proto=i2c|i2c.sensor=tsl2561|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x29|i2c.baud=100000|ENDCFG'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
```

Expect `pico/s3-001/sensor/LIGHT/line`; decoded lux is in Board A's serial
`[SENSOR]` JSON. Shade and illuminate the sensor to check that lux changes.

### Sonar A

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -m 'BEGINCFG|name=SONAR_A|proto=gpio|gpio.mode=pulse|gpio.trig=10|gpio.echo=11|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
```

Expect `pico/s3-001/sensor/SONAR_A/line`; decoded `distance_cm` and any
`TIMEOUT` appear in Board A's serial `[SENSOR]` JSON. Compare readings with a
flat target at a measured distance; a frame alone does not prove accuracy.

### Sonar B

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -m 'BEGINCFG|name=SONAR_B|proto=gpio|gpio.mode=pulse|gpio.trig=6|gpio.echo=7|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
```

Expect `pico/s3-001/sensor/SONAR_B/line`; use the serial `[SENSOR]` JSON and
a measured flat target to evaluate the distance and timeout rate.

After isolated checks, restore all four Board A profiles before continuing
the combined A → B test:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -f '.\config\board_a.cfg'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
```

## Optional: Window 4 — normal PowerShell (Board A serial console)

Connect only Board A while identifying its port; multiple ESP32s can produce
multiple `COM` numbers. The firmware command console uses the native USB
socket (Espressif `VID:PID=303A:1001`). Close any other monitor on that port.

```powershell
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py -m serial.tools.list_ports -v
& $py -m serial.tools.miniterm COMx 115200 --dtr 0 --rts 0 --eol CRLF
```

Replace `COMx` with Board A's actual native port. On 21 November 2026, with
B and C unplugged, it was `COM4`; the number can change. Look for
`[MQTT] connected to broker` and decoded `[SENSOR]` JSON. `Ctrl+T`, then
`Ctrl+E` toggles local echo; `Ctrl+]` exits miniterm.

## End the test

**First, Window 3 (normal PowerShell):** stop Board A sampling while the
broker is still running. `STOP` does not switch off Wi-Fi or MQTT.

```powershell
& $pub -h $brokerIp -t 'pico/s3-001/cmd' -m 'STOP'
```

**Then:** press `Ctrl+C` in Window 2 to stop the subscriber; use `Ctrl+]`
to leave optional miniterm in Window 4; press `Ctrl+C` in Window 1 to stop
the temporary broker. Unplug Board A's USB if you are done using it. The
original Windows Mosquitto service was stopped for this test. If you want
that localhost-only service running again, use an **Administrator PowerShell**:

```powershell
Start-Service -Name mosquitto
```

That service is not the temporary D6 network listener. Leave it stopped if
you want all MQTT broker processes off for now.

## Combined Board A → Board B test — startup to shutdown

Both boards must be powered by USB, on the laptop's private 2.4 GHz Wi-Fi,
and running distinct node identities: A is `s3-001`, B is `s3-002`. Board B
has an external SPI flash module, but the commands in this section only test
MQTT sensor-frame delivery. They do not write to flash or count occupancy.
Board B's `s3-002` image was rebuilt/flashed and its heartbeat observed on
21 November 2026. Start all subscriber windows **before** sending the config
block so their status messages are visible.

### Window 1 — Administrator PowerShell: temporary broker

If the temporary broker is already running, keep its window open and skip
this block. Do not start a second listener on port 1883. Otherwise:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
Stop-Service -Name mosquitto -ErrorAction SilentlyContinue
& "$env:ProgramFiles\mosquitto\mosquitto.exe" -c '.\tools\mqtt-test.conf' -v
```

Keep this window open. The broker command stays in the foreground.

### Window 2 — normal PowerShell: watch both boards' sensor frames

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h $brokerIp -t 'pico/+/sensor/+/line' -v
```

Keep this window open. The four source topics are under `pico/s3-001/sensor/`;
bridged aliases, if received, are under `pico/s3-002/sensor/`.

### Window 3 — normal PowerShell: watch Board B status

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h $brokerIp -t 'pico/s3-002/#' -v
```

Wait for a fresh `pico/s3-002/heartbeat`. This confirms B is currently
connected. Keep this window open to see `config OK` messages and B sensor
topics; `config OK` alone does not prove sensor delivery.

### Window 4 — normal PowerShell: configure all four sources

Paste this **one complete block** after both boards have connected. It
replaces the boards' current runtime sensor configurations; it does not
change wiring, flash firmware, or erase the external SPI flash. `CLEAR` on B
also removes the earlier one-source AHT bridge, so all four B mappings are
sent again here.

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"

# Board A: physical AHT, light, Sonar A, and Sonar B.
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -f '.\config\board_a.cfg'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'

# Board B: bridge each of A's four sensor-frame topics to a local alias.
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/config' -f '.\config\board_b_ml.cfg'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
```

Window 2 should show A's `AHT`, `LIGHT`, `SONAR_A`, and `SONAR_B` topics
and B's `A_AHT`, `A_LIGHT`, `A_SONAR_A`, and `A_SONAR_B` aliases **when each
source produces frames**. If a B alias is missing, first check whether the
matching A source appears in the same window. In the first one-source B test,
B acknowledged `A_AHT` but only B heartbeats were visible afterward; that
does not establish an AHT bridge failure or success. Broker `PUBLISH` lines
and `config OK` only show transport or configuration, respectively. An
`s3-002/sensor/<alias>/line` frame is the MQTT bridge evidence.

On 21 November 2026, Board A's AHT initially returned `error=-4` and no
MQTT frame. After restarting its runtime configuration without moving wires,
AHT reported valid temperature/humidity and published frames. A 12-second
subscriber check with the four-sensor Board A configuration then observed on
Board B: `A_AHT` 6 frames, `A_LIGHT` 6, `A_SONAR_A` 24 and `A_SONAR_B` 24.
This verifies all four A → B transport paths in that run; it does not show
that intermittent sensor errors are resolved or that B has computed occupancy.

### Shut down the combined test

While Window 1's broker is still running, enter this in **Window 4**:

```powershell
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
```

Press `Ctrl+C` in Windows 2 and 3 to stop both subscribers, then `Ctrl+C`
in Window 1 to stop the temporary broker. Unplug the boards if finished.
The optional original Windows Mosquitto service can be restarted from an
**Administrator PowerShell** with `Start-Service -Name mosquitto`; that
localhost-only service is separate from the temporary network listener.

## Board C LCD and Board B → C mock MQTT test

For real AHT, light, and **separate Sonar A/B distances** on the horizontal
LCD, use the [interim live relay procedure](docs/d6/LIVE_LCD_TWO_SONARS.md)
after flashing Board C's updated firmware. This section retains the synthetic
Board B fixture and its earlier test record.

This is a **separate display/transport test**. Board B's `D6MOCKPUB` command
publishes a synthetic snapshot; it does not use Board A's sensor readings,
calculate occupancy, run ML, or write the external SPI flash. Board C's
isolated `LCDTEST` remains available independently of this MQTT path. The
D6 snapshot view uses the verified 284 x 76 landscape orientation for every
accepted snapshot, including future live snapshots.

On 21 November 2026, the bare N16R8 LCD board appeared as native USB
`COM13` with node ID `s3-003`. The `s3-003` image was rebuilt for the current
private Wi-Fi/broker address, passed its build audit, and flashed with hash
verification. After a manual RST press, C sent MQTT heartbeats. COM numbers
can change, so identify the LCD board before opening a monitor or flashing.

### Start: broker and Board C console

Keep the temporary broker from [Window 1](#start-window-1--administrator-powershell-broker)
running. Power Boards B and C from USB and keep the laptop on the same
private 2.4 GHz Wi-Fi. Use a **normal PowerShell** to identify C's native
port, then open its console (replace `COM13` if Windows assigned another
port):

```powershell
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py -m serial.tools.list_ports -v
& $py -m serial.tools.miniterm COM13 115200 --dtr 0 --rts 0 --eol CRLF
```

The console occupies this window. If C is silent and has no MQTT heartbeat
after a fresh flash, press and release **RST once, without BOOT**, then
reopen the console if Windows disconnects the port. On this board the manual
RST was needed after flashing. In a second **normal PowerShell** window,
watch C's heartbeat and B's snapshot topic:

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h $brokerIp -t 'pico/s3-003/heartbeat' -t 'd6/board-b/snapshot' -v
```

Leave the subscriber running. A fresh `pico/s3-003/heartbeat` confirms C is
connected to the broker; a USB COM port alone does not.

### Check the isolated LCD first

Type this **inside C's miniterm window** and press Enter:

```text
LCDTEST 76 284 82 18 00 0
```

Expect `[LCD] test complete result=0` and visibly red, green, blue and black
bands with `LCD READY`. The screen is the decisive check because this LCD
has no connected readback line. The 21 November test passed in both console
output and the user's visual check. `LCDTEST` ends the D6 view mode, so send
`D6VIEW` again before every mock snapshot test that follows a pattern test.

For a separate orientation check, `LCDTEST 284 76 18 82 60 0` shows the same
pattern across the full screen when held horizontally. The user confirmed
this on 21 November. It does not replace the original portrait `LCDTEST`.

### Send a mock snapshot from B to C

In a **third normal PowerShell** window, send these commands in order:

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"
& $pub -h $brokerIp -q 1 -t 'pico/s3-003/cmd' -m 'D6VIEW'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'D6MOCKPUB 2'
```

The subscriber should show a JSON snapshot on `d6/board-b/snapshot`.
C's serial console should print `[D6MQTT] C view result=0` followed by
`[D6MQTT] C snapshot seq=<n> occupancy=2 display=0 active=1`.
`seq` increases with B's mock publishes and may reset when B reboots.
Hold the LCD horizontally. The compact ROOM MONITOR dashboard should show
`MOCK`, `OCC 2`, distance `123.4` cm, temperature `27.5` C, humidity `65.2`%,
light `420` lux, and ML score `0.953` with model `SINE-FLOAT`. The word
`MOCK` is deliberate: these are fixture values. The v2 payload on
`d6/board-b/snapshot` includes `distance_cm`, `ml_score`, `ml_model`, and
`source` in addition to the original fields. C also accepts the original
v1 snapshot and shows `--` / `NO MODEL` where distance or ML was absent.

The original portrait test produced C's `view result=0` and
`snapshot seq=1 occupancy=2 display=0 active=1`; the user's photo showed
all portrait fields. After the landscape firmware update, C logged
`snapshot seq=5 occupancy=2 display=0 active=1`, and the user confirmed all
reference fields visible horizontally. A second `D6MOCKPUB 3` logged
`snapshot seq=6 occupancy=3 display=0 active=1`; the user saw `OCC 3` without
changing orientation. This verifies B → broker → C mock transport and
dynamic LCD refresh. Live A → B → C occupancy and live ML are still pending.

### End the Board C test

Press `Ctrl+]` in C's miniterm window and `Ctrl+C` in the subscriber window.
The display keeps the last mock snapshot until another display command or
reset. Leave the broker running if still testing A/B; otherwise stop it with
`Ctrl+C` in its Administrator PowerShell window. Unplug C when finished.

## When Wi-Fi or the broker address changes

The broker IP is compiled into each ESP32 firmware image; `mosquitto_pub`
cannot change it. Update the ignored local file in a **normal PowerShell**:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
notepad .\esp32s3_mtk\main\wifi_credentials.h
```

Set `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST` (the laptop's **Wi-Fi IPv4**) and
`MQTT_PORT 1883`. Keep the board identities distinct: A is `s3-001`, B is
`s3-002`. Do not paste the password into chat or commit the filled file.
The steps below rebuild and flash **Board A only**; unplug B and C, close
Board A's serial monitor, and check its port:

```powershell
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py -m serial.tools.list_ports -v
```

The Git Bash `tools/mtk.sh flash` helper exited while sourcing ESP-IDF on
this Windows laptop. The following **normal PowerShell** build/flash path
was used successfully on 21 November 2026 with the existing N16R8 build
directory. Replace `COMx` only after identifying Board A:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23'
$boardPort = 'COMx'
$buildDir = 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23\esp32s3_mtk\build-n16r8'
$env:MTKERNEL_PATH = 'C:\Users\brend\EMBSYS-P2-AAI15-merge-work-2026-11-23\esp32s3_mtk\third_party\mtk3smp-esp32s3'
$env:IDF_PATH = "$env:USERPROFILE\esp\esp-idf-v6.1"
$env:IDF_TOOLS_PATH = "$env:USERPROFILE\.espressif"
$env:IDF_PYTHON_ENV_PATH = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env"
$MtkCmakeBin = "$env:USERPROFILE\.espressif\tools\cmake\4.0.3\bin"
$MtkNinjaBin = "$env:USERPROFILE\.espressif\tools\ninja\1.12.1"
& .\esp32s3_mtk\tools\build.ps1 -Action build -BuildDirectory $buildDir
& .\esp32s3_mtk\tools\build.ps1 -Action flash -Port $boardPort -BuildDirectory $buildDir
```

Require a successful build/audit and flash hash verification before treating
Board A as updated. Reconnect to the temporary broker and repeat the run
block above. The current [Board A wiring](docs/d6/BOARD_A_SONAR_ONLY_WIRING.md)
and [serial sensor test](docs/d6/BOARD_A_SENSOR_TEST.md) are separate checks;
MQTT delivery does not establish sensor accuracy.
