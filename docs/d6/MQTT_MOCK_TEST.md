# Temporary Board B to C MQTT mock test

## Current result (21 November 2026)

The complete Windows broker, Board B, and Board C commands are now maintained
in [MQTT_commands.md](../../MQTT_commands.md#board-c-lcd-and-board-b--c-mock-mqtt-test).
The steps and observations below preserve the earlier 18–19 November trial;
its portrait v1 fixture and USB diagnosis are historical, not the current
Board C status.

Board C's native USB port enumerated, the `s3-003` firmware was flashed and
verified, and the user saw the isolated `LCDTEST` RGB bands and `LCD READY`.
The user also confirmed a full landscape pattern with
`LCDTEST 284 76 18 82 60 0`. Board B's `D6MOCKPUB` now publishes a v2
snapshot with distance 123.4 cm, temperature 27.5 C, humidity 65.2%, light
420 lux, ML score 0.953, model `sine-float`, and `source:"mock"`. Board C
accepted and drew it in the 284 x 76 landscape ROOM MONITOR dashboard.
The user confirmed all fields visible, then confirmed the LCD changed from
`OCC 2` to `OCC 3` on a second snapshot. The console reported
`C snapshot seq=5 occupancy=2 display=0 active=1` and
`C snapshot seq=6 occupancy=3 display=0 active=1`. The mock values are not
Board A measurements or B's live occupancy/ML result. `LCDTEST` remains an
independent diagnostic; send `D6VIEW` after it or after a reboot to return
to the MQTT dashboard.

## Earlier trial and commands (18–19 November 2026)

This test is opt-in and uses synthetic values. It does not establish live Board A acquisition, flash logging, or ML inference. It leaves the isolated `LCDTEST` command intact.

## Local configuration

1. Connect the laptop, Board B and Board C to the same private 2.4 GHz Wi-Fi. Find the laptop's Wi-Fi IPv4 address with `ipconfig` in PowerShell.
2. Fill the existing blank `esp32s3_mtk/main/wifi_credentials.h` in this D6 worktree. It is gitignored; `wifi_credentials.h.example` is the tracked template. Set `WIFI_SSID`, `WIFI_PASS` and `MQTT_HOST` (the laptop's Wi-Fi IPv4 address). Leave `MQTT_PORT` at 1883. Do not commit the filled file.
3. Install [Mosquitto for Windows](https://mosquitto.org/download/) if absent. In PowerShell, start the temporary broker with:

   ```powershell
   & "$env:ProgramFiles\mosquitto\mosquitto.exe" -c "C:\Users\brend\EMBSYS-P2-AAI15-D6-Integration\tools\mqtt-test.conf" -v
   ```

   Keep that window open. Allow TCP 1883 on Windows **Private** networks if prompted. This test configuration allows anonymous clients on the private network; close the broker after testing.

## Verify the laptop broker

In a second PowerShell window:

```powershell
$brokerIp = "YOUR_LAPTOP_WIFI_IPV4"
& "$env:ProgramFiles\mosquitto\mosquitto_sub.exe" -h $brokerIp -t "d6/board-b/snapshot" -v
```

In a third window:

```powershell
$brokerIp = "YOUR_LAPTOP_WIFI_IPV4"
& "$env:ProgramFiles\mosquitto\mosquitto_pub.exe" -h $brokerIp -t "d6/board-b/snapshot" -m '{"v":1,"seq":1,"occupancy":2,"temperature_c":27.5,"humidity_pct":65.2,"light_lux":420}'
```

The subscriber should print the topic and JSON. Using the Wi-Fi IP matters: the Windows installer may also start a separate localhost-only Mosquitto service. This proves the laptop Wi-Fi broker path only.

## Build and run the actual B to C test

With each board connected alone, build and flash Board B as node `s3-002`, then Board C as node `s3-003` using the repo's `tools/mtk.sh` with `BOARD=n16r8`, the respective `NODE`, and the actual `PORT=COMx`. The two USB-C sockets can enumerate as different COM ports. Use the native USB console for commands and confirm the boot banner names the intended node. Keep the other board unplugged while flashing; then connect both for the test.

Both consoles should report Wi-Fi connected and `[MQTT] connected to broker`. C should report `[D6MQTT] C snapshot subscribe OK`. On Board C's serial console type `D6VIEW` and Enter. It should report `view result=0` and show `WAIT B MQTT`.

On Board B's serial console type `D6MOCKPUB 2` and Enter. B should report a queued snapshot with sequence 1. The laptop subscriber should see the JSON from B. C should report `C snapshot seq=1 occupancy=2 display=0 active=1` and show `ROOM MON`, `MOCK MQTT`, occupancy 2, temperature 27.5 C, humidity 65.2%, light 420 lux, sequence 1, and `B > C OK`. Repeat `D6MOCKPUB 3` to verify a changing field and sequence 2.

The fixture contract is exactly `d6/board-b/snapshot` with keys `v`, `seq`, `occupancy`, `temperature_c`, `humidity_pct`, `light_lux` in that order. C rejects malformed, out-of-range, duplicate, and out-of-order snapshots. These values are synthetic and the display labels them `MOCK MQTT`. `D6VIEW` is opt-in on C; `LCDTEST 76 284 82 18 00 0` still runs the independent RGB test. Run `D6VIEW` again to return to the mock display. This temporary viewer does not yet mark timed-out data stale, persist to flash, or calculate occupancy/ML from Board A.

## Observed on 18–19 November 2026

- Mosquitto 2.1.4 was installed on the Windows laptop. The temporary listener accepted a publish/subscribe exchange through the laptop's Wi-Fi IPv4 address, not only localhost.
- Both N16R8 images compiled and passed the repository's main/kernel audit with the private credentials file. The credentials file is ignored by Git.
- The physical ESP32 previously recorded as Board B was identified by its read-only chip MAC on the CH343 port. It was restored to `s3-002` after a mistaken C flash. Flash hashes verified, and a fresh `pico/s3-002/heartbeat` arrived.
- An MQTT `D6MOCKPUB 2` command sent to B produced `d6/board-b/snapshot {"v":1,"seq":1,"occupancy":2,"temperature_c":27.5,"humidity_pct":65.2,"light_lux":420}` at the laptop subscriber. This verifies **B publishing**.
- The separate physical LCD board did not enumerate as a Windows COM device when connected alone. Its power LED lit. The same cable and laptop port detected B, but neither USB-C socket on C enumerated. Temporarily disconnecting only LCD VCC at E4 also did not restore USB enumeration. C was **not flashed in this session**; no fresh C heartbeat is expected from its earlier offline firmware. The exact USB fault is not established. **C receiving and displaying the MQTT snapshot has not passed**; restore LCD VCC at E4, inspect C's physical USB connection, and run the C steps above before claiming a B-to-C result.

## Retry on 19 November 2026

With B unplugged, C again produced no Windows USB/COM device through the proven data cable on its COM socket. Windows also listed no newly failed USB device. Removing only the ESP32 3V3 feed from breadboard hole A4 while USB was unplugged, then reconnecting USB, did not restore enumeration. The user restored 3V3 to A4 and left C unplugged. No C firmware flash or B-to-C display claim was made. The next hardware isolation is a bare-ESP USB test or socket inspection; changing a Windows COM number cannot help until USB enumerates.
