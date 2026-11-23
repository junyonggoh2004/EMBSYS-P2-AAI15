# Failure response for the classroom system

Board A reads the doorway and room sensors. Board B maintains occupancy, runs inference and publishes the results. Board C displays them. A failed doorway measurement can make the running count uncertain. A failed room measurement affects only the results that depend on it. In both cases the boards should identify the affected value and keep independent functions running.

## What is already checked

- On `main`, the ML pipeline checks that every bound input is valid and recent, with a default maximum age of 5 seconds and maximum input skew of 2.5 seconds. It skips inference on a bad input and reports `MISSING_OR_STALE` or `SAMPLE_SKEW`. An inference or output validation error also produces an error result. These diagnostic results print on the console. The LCD cannot rely on receiving an error message when the result stream stops. See the [ML flow](ML_FLOW.md) and [runtime configuration](LIVE_SETUP_PROPOSAL.md).
- `ml_setup` validates the input mapping and model shape. Rejected setups keep the previous configuration, and a model load failure attempts to reload the previous model. The model upload path checks the upload and uses two slots so an interrupted upload can leave the previous model available. See the [D4 evidence](../../README.md#5-d4-task-checklist).
- The checked [D6 wiring and sensor config](https://github.com/junyonggoh2004/ES-P2-AAI15/blob/1a93aee/config/board_a.cfg) register two periodic ranging devices. The proposed two motion triggers and separate entry/exit counting pair still need integration. The [D6 LCD test](https://github.com/junyonggoh2004/ES-P2-AAI15/blob/1a93aee/docs/d6/MQTT_MOCK_TEST.md) exercised mock snapshots. It can hold the last screen after messages stop, so a live display needs an explicit stale state.

## Proposed responses by board

### Board A sensors and crossing detection

**Environmental sensors.** Check the driver status, whether the expected bytes arrived, and whether decoded values are finite and within the supported physical range. AHT temperature and humidity come from one frame. If that frame fails, mark both fields invalid for that sample. A TSL2561 bus error invalidates lux alone. A valid zero-lux sample still represents darkness. Publish the sensor identity and a reason with invalid fields, or let the consumer expire the reading if A cannot publish a status. Use bounded retries and reinitialise an affected I²C device after repeated failures. A persistent shared-bus fault may disable both I²C feeds. The doorway GPIO path can still run. Clear the fault when a fresh sample passes validation.

**Motion and ranging.** Use idle, capture, and rearm states on A with the board's local monotonic time. A qualifying motion reading opens one bounded sampling window. Stagger the pings so one receiver does not use another transmitter's echo. Only an unambiguous completed crossing can create an entry or exit event. Echo timeouts, invalid distances, an expired window, an ambiguous sequence, or a stuck occupied zone should close or inhibit the affected window without changing the count. Rearm after the zone clears. Tune the motion threshold, spacing and time limits with measured doorway trials.

**Reduced coverage.** With one dead motion trigger, continue using the remaining trigger only if the team has tested that it still sees both intended paths in time. If a required counting channel fails or a crossing could have been missed, mark occupancy uncertain. Keep the last confirmed count for inspection. Resume trusted counting after fresh sensor checks and an operator confirms or resets the baseline. A passing sensor self-check cannot reconstruct crossings missed during the outage.

### Board B inference, occupancy and storage

| Failure | Response and reason |
|---|---|
| Input to a model is missing, stale, out of range or from the wrong source | Skip only the affected model. Comfort requires both AHT fields from a valid reading. Missing lux stops the AHT-plus-light sine demo while comfort can continue. Missing or garbled ultrasonic data cannot be padded with zeros for live classification. A synthetic replay is labelled as a replay. |
| The interpreter returns an invalid result or the model cannot be loaded | Suppress the affected output and any rule it feeds. Keep raw sensor telemetry and verified doorway counting available. Keep or reload the last working model when recovery succeeds. If recovery fails, disable inference, report the reason and require a valid model selection before resuming. Wait for fresh inputs after a model or binding change. |
| A doorway event is duplicated, missing or reordered | Give each event an identity made of A's node ID, boot ID and a sequence number. B processes each accepted identity once and checks for gaps. Ignore duplicates. If a gap cannot be recovered from the source, mark occupancy uncertain and reconcile the baseline. MQTT QoS 1 can redeliver an event. It does not prove that every real crossing reached B. B remains the only board that updates the count. |
| B restarts or the external SPI flash stops accepting records | Report storage failure and show the count as unsaved while B can still maintain it in RAM. Keep retries bounded and allow live room telemetry to continue. After restart, restore occupancy only when a verified checkpoint and complete event history are available. Otherwise show an unknown count until the baseline is set. Keep the external W25Q32 event log separate from the firmware model slots. |

### Board C display and MQTT

Validate the snapshot version, source, sequence, units, value ranges and per-field status before drawing it. Accept live snapshots from the expected Board B source. Keep mock and replay values clearly labelled. Use C's local elapsed time since the last valid update to expire each field. A dropped packet, a duplicate sequence, or a fresh heartbeat must not refresh the age of an older reading. Stamp each field with its sample validity at B so a new snapshot cannot make an unchanged stale input appear fresh. Separate board uptime counters cannot be subtracted to measure age.

For the demo, propose one snapshot every 2 seconds and a display link timeout after 6 seconds without an accepted snapshot. That permits two missed updates before the link is marked stale. B can report each field's age at publication using B's local receipt time, and C can increase that age by the elapsed time since it received the snapshot. Apply the 5-second ML input age limit independently at B. The 6-second link timeout does not extend it. Test these starting values with actual sampling and broker timing.

Show `STALE`, `UNAVAILABLE`, or `COUNT UNCERTAIN` on affected display fields. Valid room temperature can still update if occupancy or comfort is unavailable. Keep an old value only with a visible stale label and age. After MQTT reconnect, wait for fresh validated data and a consistent event sequence before clearing a fault. If the LCD or Board C fails, A and B can keep acquiring, recording, and reporting over MQTT and the serial consoles. The demo uses recommendations. Suppress a recommendation whose inputs failed and leave the room controls available for manual use.

## How to demonstrate this requirement

| Injected condition | What to check |
|---|---|
| Remove AHT from the active configuration or stop its feed | Comfort produces no new usable score. Lux and confirmed doorway events continue. Restore AHT and verify that a fresh reading clears the fault. |
| Withhold the counting echo during a crossing | No entry or exit is committed for that window. B shows uncertain occupancy if a crossing may have been missed. Rebaseline before treating the count as current. |
| Send the same valid crossing event twice, then simulate a sequence gap | The duplicate changes nothing. A missing event leaves B unable to assert a trustworthy count until recovery or reconciliation. |
| Stop the broker while C displays a valid snapshot | C marks the old fields stale once their deadlines pass. On reconnect, verify fresh fields are accepted and old duplicates are ignored. |
| Submit an invalid model setup or remove B's event flash | An invalid model setup is refused without stopping other sensor feeds. A storage failure is visible and does not turn an unsaved count into a verified checkpoint. |

These are design and acceptance checks for the new requirement. The proposed responses require review and hardware validation before the team claims a complete failure-handling implementation.
