# D4 Part 2: live ML setup working v1 contract

**Status:** Sitt has adopted this working v1 contract for implementation. The JSON command, local sensor bindings, and console/MQTT result routes are implemented. A one-board MQTT control and sensorless replay check passed on 19 November 2026. Team review can refine a later version. Physical sensor continuity, remote input, uploaded-model selection, on-board rule/LCD output, and receiver-board delivery remain open. The D4 tasks are in [README section 5](../../README.md#5-d4-task-checklist).

## Working v1 command

Send one JSON payload to `pico/<node>/cmd`. It names the bundled model, ordered input bindings, and one result destination. The board applies a valid setup without stopping sensor sampling. This command is separate from `BEGINCFG` and the combined sensor `configs`/`rules` JSON.

```json
{
  "ml_setup": {
    "version": 1,
    "model": "sine-int8",
    "inputs": [
      {
        "index": 0,
        "source": { "kind": "local", "name": "AHT" },
        "field": "temperature_c",
        "min": -10,
        "max": 50
      }
    ],
    "output": { "kind": "publish" }
  }
}
```

`inputs` must have contiguous indices from zero. The sine models can combine one or more normalized bindings into one input, as the existing `ML BIND` path does. The live path accepts `sine-float`, `sine-int8`, `autoencoder` (exactly 16 bindings), and `comfort-summer` (temperature then humidity). `people-counter` is available for replay but its 31 inputs exceed the current 16-binding live path. MQTT-uploaded models can run through the D1 rule-driven path; selecting one with `ml_setup` or the `ML` commands is still task 3.2 work.

For output, `{"kind":"publish"}` sends a non-retained QoS 1 result on `pico/<node>/ml/result`. `{"kind":"console"}` sends results to serial only. Console diagnostics also print when publishing is selected. The result includes model, generation, timestamp, input source, outputs, invoke time, and status; vector results use a compact payload to stay below the 512-byte MQTT payload limit. A local rule or LCD destination is rejected until a consumer is implemented; changing v1 to accept one must include a local-consumer test. A disconnected broker or full queue can prevent delivery, so a successful inference does not by itself prove receipt on another board.

The board sends a non-retained QoS 1 acknowledgement to `pico/<node>/ml/status`: `{"ok":true,"reason":"applied","generation":N}` or `ok:false` with a reason and unchanged generation. A successful setup enables ML, resets the ML sample cache, clears any prior scalar threshold, and waits for fresh input. Sensor sampling continues without `STOP`. The command must be shorter than 2048 bytes. The serial line buffer is 512 bytes, so a 16-binding setup belongs on MQTT.

The working v1 wire format and topics above are the implementation baseline. If team review changes them, update the contract, tests, and version together; final team agreement is not a gate for Sitt's current work.

## Apply and failure rules

1. Parse the bounded JSON. Reject unknown or duplicate keys, wrong types, non-finite numbers, duplicate/missing indices, unsupported model/field pairs, `max <= min`, unavailable destinations, and unavailable sources without changing the active setup.
2. Validate the model shape and every sensor binding before committing a new setup.
3. On success, swap the model, bindings, and output selection; increment the generation, clear the ML sample cache, and wait for fresh input. Sensor acquisition and unrelated rules continue.
4. On a rejected setup, keep the previous active model and bindings. Return `ok:false`, a reason, and the previous generation. A model-load failure is reported; preserving the old model depends on a successful reload.
5. Send the status acknowledgement on MQTT and log it on serial. The receiver should use the generation to distinguish old and new results.

The older `ML PRESET`/`ML MODEL` path still requires `STOP`. The JSON path currently switches bundled models and local bindings while sampling is active.

## Remote source and local destination work still open

The intended remote binding is `"source":{"kind":"remote","node":"pico-002","name":"AHT"}` with the same `field`, `min`, and `max` keys. The current ML pipeline rejects it; it never silently falls back to a local sensor. The unmerged D6 branch has a strict sensor-frame parser and remote profile decoder. Integrate those interfaces before adding subscriptions, typed remote fields, freshness checks, and old-follow removal to the ML path, avoiding a second raw-frame decoder. On two boards, verify local → remote → local switching, source identity, fresh values, and no inference from a stopped or stale source.

A future local output can trigger a rule or LCD action. The current `console` route is diagnostic output, not a local rule/LCD consumer. Demonstrate a real local consumer and published result using the same model/input before marking task 3.4 fully done. The receiving-board check must show actual delivery, not merely a successful local MQTT publish call.

## 19 November 2026 one-board MQTT and replay check

The ESP32-S3 on `COM6` (node `sitt-s3-01`) was built with the kernel audit and flashed with verified hashes. The pinned ESP-IDF build reported `v6.1-dev-2300-g17a74c925c`. The board and laptop used the same 2.4 GHz Wi-Fi; Mosquitto 2.1.4 ran on the laptop. Credentials stayed in the ignored `wifi_credentials.h` and are not recorded here. The tested firmware source was copied unchanged into this main worktree and built there too.

- The broker received `pico/sitt-s3-01/heartbeat` with `{"uptime_s":12}`; serial reported MQTT connected.
- With no physical sensor attached, the AHT profile from `config/three_sensors.cfg` was temporarily accepted as `OK:AHT`; `RUN` exercised the control path, not sensor sampling.
- MQTT `ml_setup` accepted `sine-int8` at generation 2 and a live switch to `sine-float` at generation 3. An unknown model returned `ok:false` with generation still 3.
- In a later check, a valid `sine-float` setup with `output: publish` was accepted at generation 7. After `STOP`, `ML REPLAY 1.5707963` produced a QoS 1 `ml/result` with `input_source:"replay"`, `outputs:[0.9956719]`, `invoke_us:806`, and `status:"OK"`.
- Changing the same setup to `output: console` was accepted at generation 9. After `STOP` and the same replay command, serial showed the result; no MQTT result arrived during a seven-second observation.
- In another check, a valid publish setup reached generation 13. An unsupported `output: local` and malformed JSON each returned `ok:false` with generation still 13. After `STOP`, the same replay command still published the previous `sine-float` result (`outputs:[0.9956719]`, status `OK`). A heartbeat arrived afterward.
- Native host tests cover validation, failed load, and the previous model continuing to publish after rejected setup. The ESP-IDF firmware build and post-build kernel audit passed on main.

These checks establish MQTT control, bundled-model switching, invalid-setup rejection, one-board result routing, and replay inference without a sensor. They do not establish physical sensor continuity, an uploaded-model switch, remote input, a local rule/LCD action, or another board receiving a result. No synthetic value is treated as a sensor reading.

## Repeatable checks and sensor-equipped handoff

1. On the broker, subscribe to `pico/<node>/ml/status` and `pico/<node>/ml/result`; keep a serial log at the board's configured rate. Use distinct node IDs for each board. Record the board, firmware commit, broker, model, and command timestamps; omit passwords.
2. For a sensorless repeat, configure the AHT profile only as a temporary binding, send `RUN`, then send the working v1 JSON above. Switch `model` to `sine-float` and check an increased generation. Send an unknown model, unsupported output, and malformed JSON; verify `ok:false` and unchanged generation. Then `STOP` and send `ML REPLAY 1.5707963`; verify a result from the previously accepted model. Run the publish and console routes separately. `STOP`/`CLEAR` when finished. This is a control/replay check only.
3. A teammate with a connected, working AHT sensor should use the approved pinout and normal sensor configuration, confirm real sensor messages first, then run the same model switch **without `STOP`**. Record continuous sensor messages, new-generation ML results, and a rejected setup followed by more real readings and results. This closes the physical-sensor regression for 3.2 and 3.5.
4. After remote-source support is integrated, use two ESP32-S3 boards on the same broker with unique IDs. Publish real readings from board A; switch board B's `ml_setup` from local to remote and back. Check source identity, result generation, stale-feed refusal, and no mixed-generation output. Have the receiving board confirm it got the published result.
5. After a local rule/LCD consumer exists, run the same model/input to a local action and to MQTT; save the local action, received payload, and numeric outputs. Update README rows 3.3, 3.4, 3.6, and 3.7 with logs and observed results only after those checks pass.