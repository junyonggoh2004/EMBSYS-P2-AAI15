# One-board MQTT smoke test on Windows

Use `main` and open `AAI15-MTK.code-workspace` in VS Code. These steps verify sensor telemetry to the laptop broker and an ML command from the broker to the ESP32. Use the existing three-sensor wiring.

## 1. Start the broker

Install Mosquitto and its command-line clients if needed. Put the laptop and ESP32 on the same private network. Run `ipconfig` and note the laptop Wi-Fi adapter’s IPv4 address. The examples use `192.168.1.23`. [Mosquitto Windows download](https://mosquitto.org/download/)

In one VS Code PowerShell terminal, replace the example address and leave the broker running. This temporary listener accepts unauthenticated clients on the selected interface. [Mosquitto listener configuration](https://mosquitto.org/man/mosquitto-conf-5.html)

```powershell
$broker = '192.168.1.23'
$conf = Join-Path $env:TEMP 'aai15-mqtt-smoke.conf'
@("listener 1883 $broker", 'allow_anonymous true') | Set-Content -LiteralPath $conf -Encoding ASCII
& 'C:\Program Files\mosquitto\mosquitto.exe' -c $conf -v
```

## 2. Configure Wi-Fi and build

In a second PowerShell terminal, open the `esp32s3_mtk` workspace folder. Copy the template if your local credentials file does not exist.

```powershell
if (-not (Test-Path -LiteralPath .\main\wifi_credentials.h)) {
    Copy-Item -LiteralPath .\main\wifi_credentials.h.example -Destination .\main\wifi_credentials.h
}
```

Edit `main/wifi_credentials.h` with your 2.4 GHz Wi-Fi name and password. Set `MQTT_HOST` to the laptop’s IPv4 address, `MQTT_PORT` to `1883` and `NODE_ID` to `"s3-001"` for the topic examples below. The credentials file is ignored by Git. Refresh the main source timestamp to force it to compile with the newly created header, then build.

```powershell
(Get-Item -LiteralPath .\main\main.c).LastWriteTime = Get-Date
powershell.exe -NoProfile -File .\tools\build.ps1 build
```

## 3. Flash and verify the local INT8 model

Close any serial monitor and run **Terminal → Run Task → MTK: flash** on COM5. After flashing, use the same PowerShell terminal in `esp32s3_mtk` to load the full sensor preset with `sine-int8`.

```powershell
powershell.exe -NoProfile -File .\tools\build.ps1 ml -Port COM5 -Model sine-int8
```

Open **MTK: monitor raw** on COM5. Look for valid sensor readings, numeric ML output for `sine-int8`, and `[ML-ACCEL] esp_nn_fc_s16_esp32s3 vector_calls=2`. The trace appears on the first successful inference after loading the model, so if you miss it, close the monitor and repeat the loader step. The broker terminal should show a client connection from the board.

## 4. Verify both MQTT directions

In another PowerShell terminal, substitute the same laptop IPv4 address and subscribe to sensor lines. Keep this terminal open. The payloads contain the sensor frame text.

```powershell
& 'C:\Program Files\mosquitto\mosquitto_sub.exe' -h 192.168.1.23 -p 1883 -v -t 'pico/s3-001/sensor/+/line'
```

In a separate terminal, send one ML command.

```powershell
& 'C:\Program Files\mosquitto\mosquitto_pub.exe' -h 192.168.1.23 -p 1883 -q 1 -t 'pico/s3-001/cmd' -m 'ML SHOW'
```

The test passes when the subscriber receives sensor messages and the board’s serial monitor prints `ML: model=sine-int8 enabled=1 ...` in response to the command. Save a short capture from each terminal. The real two-board demonstration and live JSON configuration have separate acceptance checks.
