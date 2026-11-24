# Board A Stage 1 bring-up: 18 November 2026

The bare N16R8 Board A used the Jun Yong V2-derived rows 4-9: 3V3 A4, SDA GPIO1/A5, SCL GPIO2/A6, GND A7, Sonar A TRIG GPIO4/A8, ECHO GPIO5/A9. Sonar B and radar were disconnected. All tests used the native USB console at COM4, with volatile configurations and sampling stopped after each run.

| Check | Observation |
|---|---|
| AHT only, 0x38 at 100 kHz | Six `OK`, error 0 reads: approximately 29.85-29.87 C and 77.72-77.78% RH |
| TSL2561 only, 0x29 at 100 kHz | `OK`, error 0; about 1 lux in ambient light and 4-14 lux under a phone flashlight |
| Three-sensor config | `config/three_sensors.cfg` accepted all three profiles; AHT and light stayed `OK` while Sonar A timed out |
| Sonar A, 30-50 cm flat target | Repeated `TIMEOUT`, error -1, no ECHO pulse with 10 us and 100 us triggers |
| GPIO4-to-GPIO5 temporary loopback | Six `OK` captures near 1,010 us using a 1,000 us trigger; ESP pin/capture path works |
| Sonar A reconnected | Still repeated `TIMEOUT`; sampling stopped |

The loopback jumper was removed and Sonar A was restored to VCC C4, GND C7, TRIG E8, ECHO E9. AHT and light are live inputs; Sonar A is not validated. Inspect its printed pin labels, module-side jumper contacts, power and ground. No Sonar B or radar test was done in this Stage 1 run. This is not a complete D6 crossing test.

## Replacement sonar follow-up

A second user-confirmed 3.3 V HC-SR04+ module was tried on the same GPIO4/5 signal path with a flat target. It also returned repeated TIMEOUT and no ECHO pulse. Moving its power leads to B4 (3V3 row) and blue rail N2 (bridged from GND row 7), and replacing both complete sensor-side TRIG and ECHO jumper leads, did not change the result. The GPIO4-to-GPIO5 loopback had passed earlier. Sampling was stopped after each trial. A sensor-side electrical/power or module-variant issue remains unresolved; do not treat Sonar A as validated for D6 crossing logic. No Sonar B GPIO6/7 test was run in this follow-up.

Moving only the replacement sonar GND from the blue rail back to C7 (same row as the working AHT/light ground), while retaining its new TRIG/ECHO leads and VCC at B4, still yielded repeated TIMEOUT/no ECHO. The stage-one Sonar A fixture remains unverified. The next decisive check is voltage at the module's VCC/GND pins under power, or an independently verified sonar/power fixture; neither has yet been measured here.


## Later Board A isolation checks on 18 November

These are later tests of the spacious 65-row proposal,
with the AHT and light temporarily removed. They do not supersede the earlier
Jun Yong V2 three-sensor record above. All console tests used COM4; each volatile
profile was stopped and cleared afterward.

| Test | Observed result |
|---|---|
| Sonar B GPIO6/7, power through proposed top 3V3/GND rails | Pulses of about 10-35 us, all `INVALID_READING`; no valid distance |
| Sonar B GPIO6/7, power moved directly to row 4/7 | Valid distances, mostly about 194-200 cm in one run and including about 30 cm in a later target run; one later timeout. This establishes usable captures, not calibrated distance accuracy or a complete D6 pass. |
| Known-working Sonar B module on Sonar A GPIO4/5, rows 8/9 | Thirteen `OK` captures around 29-31 cm; ESP pins and signal rows can capture a real echo. |
| Sonar A module with Sonar B's proven four leads on row 4/7 and rows 8/9 | Twelve `OK` captures, mostly 25-26 cm, and one timeout. Sonar A's module can respond. |
| Sonar A module with its original four leads, even when their breadboard ends were moved | Repeated `TIMEOUT`. The original lead set or its contacts are suspect; the failing individual lead was not identified. |
| Both sonar profiles polled periodically at 1 Hz | Sonar A timed out; Sonar B returned `-6` (`HAL_ERR_BUSY`) because the single pulse-capture resource was already in use. This was a simultaneous-poll test error, not evidence that B's module failed. D6 timed pairing must serialize captures. |
| LD2450 on UART GPIO17/18 | Configuration accepted at 256000 baud, 8N1, but no raw UART bytes were received, including after radar GND moved from the uncertain lower rail to direct GND row E7. A 115200-baud check was silent too. Radar power at its connector and the TX electrical path remain unmeasured. |

The demonstrated direct-row sonar power hookups were temporary diagnostics while
AHT and light were removed. The spacious diagram's rail-fed Sonar B power and
lower-rail radar GND have **not** passed electrical validation. Do not infer
that a rail or jumper is continuous from its colour or position. The radar
is not yet validated. No two-sonar or full A-board use-case pass is claimed.

## Follow-up console check: 19 November 2026

An isolated `SONAR_A` profile on GPIO4 TRIG/GPIO5 ECHO, with a 10 us trigger
and 20,000 us pulse timeout, again produced repeated `TIMEOUT` / `error=-1`
with null distance and echo fields. The supplied capture shows errors through
`total=19`. This is a missing usable echo in an A-only run, not the `-6`
capture-busy result from starting both periodic sonars together. The earlier
physical swap remains the stronger isolation evidence: the A module and
GPIO4/5 path produced valid distances with the proven B lead set; A's original
complete jumper lead set remains suspect. No replacement-lead retest or new
successful A result was supplied for this follow-up.

Windows later enumerated a CH343 USB-to-serial bridge as COM9
(`VID:PID=1A86:55D3`). `miniterm` opened that port and printed its banner, but
that does not prove the firmware's REPL answered. The console for this build
uses the native ESP32-S3 USB-Serial-JTAG socket; find its current COM number
rather than reusing COM4 or COM9. `miniterm` local echo is off by default, so
invisible keystrokes alone are not proof that the serial port cannot accept
input. No native-port `HELP`/`SHOW` result has been supplied for this attempt.
