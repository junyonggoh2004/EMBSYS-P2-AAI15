# D6 MQTT commands — Board A sensor telemetry

This is the **tested Board A → laptop Mosquitto** procedure for the ESP32-S3
N16R8 node `s3-001`. Board A uses the four radar-free profiles in
[`config/board_a.cfg`](config/board_a.cfg): AHT, light, Sonar A and Sonar B.
It proves sensor frames reach the broker and a laptop subscriber. It does not
prove accurate sonar distance, doorway occupancy, Board B processing, SPI
flash writes or a live Board C display. Board B commands and evidence will
be added **after its physical MQTT integration test**.

## Before each session

- Connect the laptop and Board A to the same private **2.4 GHz Wi-Fi**.
- Power Board A from USB. Its firmware must be built with the same Wi-Fi name,
  password and laptop broker IP in the ignored local
  `esp32s3_mtk/main/wifi_credentials.h`. Do not commit that private file.
- Use a **normal PowerShell** to check the laptop's current Wi-Fi address:

  ```powershell
  cd 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
  $brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
  $brokerIp
  ```

  The observed address on 21 November 2026 was `192.168.1.126`; it may change.
  If it differs from the address compiled into Board A, update the ignored
  credentials file and rebuild/flash Board A before proceeding. See
  [When Wi-Fi or the broker address changes](#when-wi-fi-or-the-broker-address-changes).

## Start: Window 1 — Administrator PowerShell (broker)

Check whether a broker is already listening. **Do not start a second copy**
on port 1883. The normal Windows Mosquitto service may listen on localhost
only; the temporary D6 listener must accept the ESP32 on the Wi-Fi network.

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
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
`RUN`. The `.../sensor/<name>/line` payload contains **hex bytes**, not
human-readable measurements. Use the optional serial console below for
decoded temperature, humidity, lux and distance.

## Run: Window 3 — normal PowerShell (send Board A commands)

Wait until Board A is connected. Then run these lines in order:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
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

## When Wi-Fi or the broker address changes

The broker IP is compiled into the ESP32 firmware; `mosquitto_pub` cannot
change it. Update the ignored local file in a **normal PowerShell**:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
notepad .\esp32s3_mtk\main\wifi_credentials.h
```

Set `WIFI_SSID`, `WIFI_PASS`, `MQTT_HOST` (the laptop's **Wi-Fi IPv4**),
`MQTT_PORT 1883` and `NODE_ID "s3-001"`. Do not paste the password into
chat or commit the filled file. Rebuild and flash only the identified Board A;
unplug B and C, close Board A's serial monitor, and check its port:

```powershell
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py -m serial.tools.list_ports -v
```

The Git Bash `tools/mtk.sh flash` helper exited while sourcing ESP-IDF on
this Windows laptop. The following **normal PowerShell** build/flash path
was used successfully on 21 November 2026 with the existing N16R8 build
directory. Replace `COMx` only after identifying Board A:

```powershell
cd 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration'
$boardPort = 'COMx'
$buildDir = 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration\esp32s3_mtk\build-n16r8'
$env:MTKERNEL_PATH = 'C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration\esp32s3_mtk\third_party\mtk3smp-esp32s3'
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
