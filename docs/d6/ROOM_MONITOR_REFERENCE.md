# Room monitor display reference

![User-supplied room monitor mockup](images/room-monitor-mock-reference.png)

The user supplied this landscape mockup as the visual reference on
18 November 2026. The actual Board C LCD is a 76 x 284 portrait panel,
so the fields must be stacked vertically. Keep units, validity, source,
and status legible. Do not present mock values as live sensor readings.

## Temporary USB relay trial

Board B ran `APP RESET` and a synthetic `APP EVENT` entry. Its application
accepted event 493239 and changed occupancy from 0 to 1. A temporary PC
serial relay sent that occupancy to Board C on `LCDMOCK 1 493239`. C
reported `result=0`; the user confirmed all portrait fields were visible.
Distance 123.4 cm, temperature 27.5 C, humidity 65.2%, light 420 lux,
and ML score 0.953 were visual fixture values from this reference, not
readings from Board A or computed ML output.

After the trial, Board B's synthetic occupancy was reset to 0. The
temporary `LCDMOCK` firmware code was removed, Board C's normal `s3-003`
D6 firmware was rebuilt and reflashed, and isolated
`LCDTEST 76 284 82 18 00 0` again returned `result=0`. Board B's
firmware was not changed. The PC relay is not the intended B-to-C
MQTT transport; live sensor, occupancy, ML, and stale-state integration
remains to be implemented.

## Private MQTT mock follow-up

The opt-in Board B to C mock snapshot path, its local broker setup, and its
limits are documented in [MQTT_MOCK_TEST.md](MQTT_MOCK_TEST.md). The firmware
keeps `LCDTEST` available independently. MQTT values remain synthetic until
Board A events and the final B application snapshot are integrated.
