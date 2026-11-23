# D6 use case commands

Use this guide from the repository root with the three ESP32-S3 N16R8 boards: A = `s3-001`, B = `s3-002`, C = `s3-003`. All boards and the laptop must use the same private 2.4 GHz Wi-Fi. Set `WIFI_SSID`, `WIFI_PASS`, and `MQTT_HOST` (the laptop's Wi-Fi IPv4 address) in the ignored `esp32s3_mtk/main/wifi_credentials.h` before building and flashing each node. `MQTT_HOST` is compiled into firmware; changing networks or laptop IP requires rebuilding and reflashing. Never commit the filled credentials file.

The commands below exercise individual Board A sensors, A→B sensor-frame bridging, Board B's live `sine-float` ML demonstration, and B→C **mock** LCD snapshots. A separate [live LCD procedure](docs/d6/LIVE_LCD_TWO_SONARS.md) displays A and B sonar distances together with real AHT/light readings through a temporary laptop relay; B's live `comfort-summer` result reaches C directly. Occupancy counting, Board B's own full snapshot publisher, and the flash-log use case remain open. Optional model switches, including `comfort-summer` and replay-only `people-counter`, are in [ML_commands.md](ML_commands.md). Sensor MQTT `/line` payloads are encoded frames; [`tools/show_live_values.py`](tools/show_live_values.py) continuously decodes them into C, %RH, lux and cm in a terminal. The broker configuration `tools/mqtt-test.conf` is for a temporary private test.

## Mac / Linux

### 1. Prepare the boards

Open a terminal in the repository root. Replace the broker IP and each serial port with the actual values. On macOS, `ipconfig getifaddr en0` can show the Wi-Fi address; on Linux, use `ip -4 -brief address`. Flash one board at a time using its own port. Skip flashing if all three already run current firmware with the correct Wi-Fi, broker IP, and node IDs.

```bash
cd /path/to/EMBSYS-P2-AAI15
BROKER_IP=YOUR_LAPTOP_WIFI_IPV4
cp -n esp32s3_mtk/main/wifi_credentials.h.example esp32s3_mtk/main/wifi_credentials.h
${EDITOR:-nano} esp32s3_mtk/main/wifi_credentials.h

BOARD=n16r8 NODE=s3-001 PORT=/dev/YOUR_BOARD_A_PORT sh tools/mtk.sh flash
BOARD=n16r8 NODE=s3-002 PORT=/dev/YOUR_BOARD_B_PORT sh tools/mtk.sh flash
BOARD=n16r8 NODE=s3-003 PORT=/dev/YOUR_BOARD_C_PORT sh tools/mtk.sh flash
```

After flashing, connect all three boards. A serial port or power LED alone does not prove MQTT connectivity; look for fresh heartbeats in step 3.

### 2. Start the temporary broker — terminal 1

Make sure no other broker is already listening on port 1883. This command remains in the foreground:

```bash
cd /path/to/EMBSYS-P2-AAI15
mosquitto -c tools/mqtt-test.conf -v
```

### 3. Watch board traffic — terminal 2

```bash
BROKER_IP=YOUR_LAPTOP_WIFI_IPV4
mosquitto_sub -h "$BROKER_IP" -t 'pico/+/heartbeat' -t 'pico/+/sensor/+/line' -t 'pico/s3-002/ml/status' -t 'pico/s3-002/ml/result' -t 'd6/board-b/snapshot' -v
```

Wait for fresh `s3-001`, `s3-002`, and `s3-003` heartbeats before sending commands. Keep this terminal open.

For **actual live measurements**, open terminal 4 in the repository root and keep it running. It subscribes to A's MQTT frames and prints only decoded readings; `--board B` shows B's relayed copies. The laptop-side broker address defaults to `127.0.0.1`, regardless of the laptop Wi-Fi address compiled into the boards.

```bash
python3 tools/show_live_values.py --board A
# In a separate terminal, to check B's received copies:
python3 tools/show_live_values.py --board B
```

`Ctrl+C` stops either viewer. A sensor error is printed as an error; invalid sonar echoes are not displayed as distances.

### 4. Test Board A sensors one at a time — terminal 3

Run the setup once, then any of the four `test_a_sensor` lines. Each call stops sampling, clears the previous sensor configuration, adds **only that sensor**, and resumes. For a sonar, put a flat target about 30–50 cm ahead and check the live cm value in terminal 4; an MQTT frame alone does not prove a valid distance.

```bash
cd /path/to/EMBSYS-P2-AAI15
BROKER_IP=YOUR_LAPTOP_WIFI_IPV4
test_a_sensor() {
  mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m STOP
  mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m CLEAR
  mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/config -m "$1"
  sleep 2
  mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m RUN
}

# AHT temperature and humidity
test_a_sensor 'BEGINCFG|name=AHT|proto=i2c|i2c.sensor=aht|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x38|i2c.baud=100000|ENDCFG'

# TSL2561 light
test_a_sensor 'BEGINCFG|name=LIGHT|proto=i2c|i2c.sensor=tsl2561|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x29|i2c.baud=100000|ENDCFG'

# Sonar A: TRIG GPIO10, ECHO GPIO11
test_a_sensor 'BEGINCFG|name=SONAR_A|proto=gpio|gpio.mode=pulse|gpio.trig=10|gpio.echo=11|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG'

# Sonar B: TRIG GPIO6, ECHO GPIO7
test_a_sensor 'BEGINCFG|name=SONAR_B|proto=gpio|gpio.mode=pulse|gpio.trig=6|gpio.echo=7|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG'
```

### 5. Run all four A→B sensor bridges — terminal 3

This restores all Board A sensors and four typed Board B aliases, then selects `sine-float` as B's default live ML demonstration. In terminal 2, expect A's `AHT`, `LIGHT`, `SONAR_A`, and `SONAR_B` topics, B's `A_AHT`, `A_LIGHT`, `A_SONAR_A`, and `A_SONAR_B` topics when sources produce frames, and B's ML status/result topics when fresh ML inputs are available.

```bash
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m STOP
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m CLEAR
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/config -f config/board_a.cfg
sleep 2
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m RUN

mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -m STOP
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -m CLEAR
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/config -f config/board_b_ml.cfg
sleep 2
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -m RUN
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -f config/board_b_sine_float.json
```

### 6. Test B→C landscape display with mock data — terminal 3

`D6VIEW` enables C's MQTT dashboard. `D6MOCKPUB` makes B publish synthetic values; it does not use A's readings. Hold the LCD horizontally and confirm `ROOM MONITOR`, `MOCK`, occupancy 2, then occupancy 3.

```bash
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-003/cmd -m D6VIEW
sleep 2
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -m 'D6MOCKPUB 2'
sleep 2
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -m 'D6MOCKPUB 3'
```

### 7. End the session

```bash
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-001/cmd -m STOP
mosquitto_pub -h "$BROKER_IP" -q 1 -t pico/s3-002/cmd -m STOP
```

Press **Ctrl+C** in the subscriber terminal, then **Ctrl+C** in the broker terminal. Unplug the boards if finished.

## Windows

### 1. Prepare the boards — normal PowerShell; Git Bash for flashing

Open **normal PowerShell** in the repository root. Confirm the laptop's Wi-Fi IPv4 address and edit the ignored credentials file. Use that address as `MQTT_HOST` for all three board builds.

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$brokerIp
if (-not (Test-Path '.\esp32s3_mtk\main\wifi_credentials.h')) {
    Copy-Item '.\esp32s3_mtk\main\wifi_credentials.h.example' '.\esp32s3_mtk\main\wifi_credentials.h'
}
notepad '.\esp32s3_mtk\main\wifi_credentials.h'
```

Flash one board at a time **only if needed**, using **Git Bash** from the same repository root. Replace each `COMx` with its current port; `python -m serial.tools.list_ports -v` lists ports. Use the ESP32's native USB socket for the firmware console.

```bash
BOARD=n16r8 NODE=s3-001 PORT=COMx sh tools/mtk.sh flash
BOARD=n16r8 NODE=s3-002 PORT=COMy sh tools/mtk.sh flash
BOARD=n16r8 NODE=s3-003 PORT=COMz sh tools/mtk.sh flash
```

After flashing, connect all three boards.

### 2. Start the temporary broker — Administrator PowerShell, window 1

Run from the repository root. This command occupies the window. If port 1883 is already used by a different broker, stop that broker first.

```powershell
Stop-Service -Name mosquitto -ErrorAction SilentlyContinue
& "$env:ProgramFiles\mosquitto\mosquitto.exe" -c (Join-Path (Get-Location) 'tools\mqtt-test.conf') -v
```

### 3. Watch board traffic — normal PowerShell, window 2

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$sub = "$env:ProgramFiles\mosquitto\mosquitto_sub.exe"
& $sub -h $brokerIp -t 'pico/+/heartbeat' -t 'pico/+/sensor/+/line' -t 'pico/s3-002/ml/status' -t 'pico/s3-002/ml/result' -t 'd6/board-b/snapshot' -v
```

Wait for fresh heartbeats from `s3-001`, `s3-002`, and `s3-003`. Keep this window open.

For **actual live measurements**, open another **normal PowerShell** window in the repository root. Keep this viewer running alongside the heartbeat/raw-frame window. It uses the laptop's local broker listener, so it does not need the laptop's Wi-Fi IP. Use `--board B` in a second viewer to see B's relayed copies. Press **Ctrl+C** to stop a viewer.

```powershell
$py = "$env:USERPROFILE\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe"
& $py .\tools\show_live_values.py --board A
# In another PowerShell window, for Board B's received readings:
& $py .\tools\show_live_values.py --board B
```

The viewer prints temperature in C, humidity in %RH, light in lux, and valid sonar distance in cm. It reports sensor errors and invalid echoes instead of showing a made-up distance.

### 4. Test Board A sensors one at a time — normal PowerShell, window 3

Run the setup once, then any of the four `Send-BoardASensor` lines. Each call replaces A's current sensor profile with only the selected sensor. Check the live-values PowerShell window above; for a sonar, use a flat target about 30–50 cm ahead.

```powershell
$brokerIp = (Get-NetIPConfiguration -InterfaceAlias 'Wi-Fi').IPv4Address.IPAddress
$pub = "$env:ProgramFiles\mosquitto\mosquitto_pub.exe"
function Send-BoardASensor([string]$config) {
    & $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
    & $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
    & $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -m $config
    Start-Sleep -Seconds 2
    & $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'
}

# AHT temperature and humidity
Send-BoardASensor 'BEGINCFG|name=AHT|proto=i2c|i2c.sensor=aht|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x38|i2c.baud=100000|ENDCFG'

# TSL2561 light
Send-BoardASensor 'BEGINCFG|name=LIGHT|proto=i2c|i2c.sensor=tsl2561|mode=poll|freq_hz=0.5|timing.timeout_ms=600|i2c.sda=1|i2c.scl=2|i2c.addr=0x29|i2c.baud=100000|ENDCFG'

# Sonar A: TRIG GPIO10, ECHO GPIO11
Send-BoardASensor 'BEGINCFG|name=SONAR_A|proto=gpio|gpio.mode=pulse|gpio.trig=10|gpio.echo=11|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG'

# Sonar B: TRIG GPIO6, ECHO GPIO7
Send-BoardASensor 'BEGINCFG|name=SONAR_B|proto=gpio|gpio.mode=pulse|gpio.trig=6|gpio.echo=7|gpio.pull=down|gpio.trig_us=10|gpio.pulse_timeout_us=20000|mode=poll|freq_hz=2|timing.timeout_ms=100|ENDCFG'
```

### 5. Run all four A→B sensor bridges — normal PowerShell, window 3

Run from the repository root. Window 2 should show the four A source topics, B's typed aliases, and B's `sine-float` ML status and results when fresh inputs arrive. See [ML_commands.md](ML_commands.md) for optional live model switches.

```powershell
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/config' -f '.\config\board_a.cfg'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'RUN'

& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'CLEAR'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/config' -f '.\config\board_b_ml.cfg'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'RUN'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -f '.\config\board_b_sine_float.json'
```

### 6. Test B→C landscape display with mock data — normal PowerShell, window 3

This checks B publishing and C drawing a changing **synthetic** snapshot. Hold the LCD horizontally. Expect `MOCK` and occupancy 2, then occupancy 3.

```powershell
& $pub -h $brokerIp -q 1 -t 'pico/s3-003/cmd' -m 'D6VIEW'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'D6MOCKPUB 2'
Start-Sleep -Seconds 2
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'D6MOCKPUB 3'
```

### 7. End the session

```powershell
& $pub -h $brokerIp -q 1 -t 'pico/s3-001/cmd' -m 'STOP'
& $pub -h $brokerIp -q 1 -t 'pico/s3-002/cmd' -m 'STOP'
```

Press **Ctrl+C** in window 2 (subscriber), then **Ctrl+C** in window 1 (broker). Unplug the boards if finished.
