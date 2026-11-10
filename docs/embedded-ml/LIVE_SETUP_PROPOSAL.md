# D4 Part 2: proposed live ML setup contract

**Status: draft for Hasif, Sitt, Jun Yong and Brendan to agree. Nothing in this document is implemented as a JSON command yet.** The agreed D4 tasks are in [README section 5](../../README.md#5-d4-task-checklist). Task 1.4 (console commands reachable over MQTT, Hasif) and task 2.1 (numeric ML output on the real S3, Jun Yong) are still prerequisites for the final live demonstration.

## User operation to agree for task 3.1

Propose one JSON payload on the existing `pico/<node>/cmd` topic. It names the model, all input bindings, and one result destination. The board applies the complete setup while sensor sampling continues. This is separate from the existing sensor `BEGINCFG` and combined `configs`/`rules` JSON so those parsers keep their current meaning.

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

`inputs` is an ordered list with contiguous indices starting at zero. The current scalar sine models can reduce one or more normalized bindings to one input, as the existing `ML BIND` pipeline does. The bundled autoencoder requires exactly 16 bindings. Supported model IDs today are `sine-float`, `sine-int8` and `autoencoder`. An uploaded model ID must not be accepted by this command until task 2.5 defines its manifest and activation rules.

For another board's reading, propose `"source":{"kind":"remote","node":"pico-002","name":"AHT"}` with the same `field`, `min` and `max` keys. The existing MQTT sensor bridge can follow a remote source, but the ML pipeline currently accepts only local scheduler configurations. Subscribing, decoding typed remote fields, freshness checks and removing an old follow on a later switch all need implementation and real two-board evidence under task 1.1. A remote source must be rejected until those pieces are ready; it must never silently fall back to a local sensor with the same name.

For `output`, propose `{"kind":"publish"}` for a non-retained result message on `pico/<node>/ml/result`, or `{"kind":"local","target":"rule"}` / `{"kind":"local","target":"lcd"}` for use on the board. Current ML output is console-only, the LCD is disabled, and no ML-to-rule adapter exists. Unsupported destinations must be rejected until task 3.4 implements them. The team still needs to agree the publish QoS, payload fields and local target semantics.

## Apply and failure rules

1. Parse the entire JSON with a bounded payload and reject unknown or duplicate keys, wrong types, non-finite numbers, duplicate/missing indices, unsupported model/field pairs, `max <= min`, and unavailable destinations or remote sources. Return a specific error; do not mutate the active setup.
2. Verify that the proposed model accepts the resulting input shape and that every referenced sensor or remote feed can supply its declared field. Model preparation belongs to InferenceTask, while SchedulerTask owns configuration and samples.
3. If preparation succeeds, swap the complete model/binding/destination snapshot at one SchedulerTask boundary. Assign a new generation, clear only the ML sample cache, and wait for fresh input. Sensor acquisition and unrelated rules continue throughout.
4. If parsing, validation, model loading or subscription fails, keep the previous active ML setup and sensor sampling running. Do not publish a success acknowledgement or a partial result.
5. Include the accepted generation and an explicit success/error reason in a status response. The status topic and exact response schema require team agreement; console logs alone are insufficient for remote control.

The current `ML PRESET`/`ML MODEL` path is **not atomic live switching**: it requires `STOP` and disables the ML pipeline during a change. It must not be presented as task 3.2 complete. This proposal also needs a maximum command size decision: MQTT receives up to 2048 bytes, whereas the current serial line buffer is 512 bytes; a 16-binding JSON setup may not fit the serial path.

## Evidence needed before marking tasks done

- **3.1:** Hasif and Sitt agree the payload, result destinations, acknowledgement and limits; record the chosen contract here.
- **3.2 and 3.5:** From MQTT, switch between two validated models while sensor JSON continues. A wrong model or malformed setup returns an error, leaves the old model producing results and never stops sensors.
- **3.3:** On two real boards, switch the ML source from local to remote and back; show the remote node/source identity, fresh values and no mixed-generation inference.
- **3.4:** Demonstrate one local consumer and one published ML result with the same input and model, and verify delivery on the receiving board.
- **3.6 and 3.7:** Save the exact commands, serial/MQTT logs, model inputs and numeric outputs; run the published sequence on hardware and update the checklist.
