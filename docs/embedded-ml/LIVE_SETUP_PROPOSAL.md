# D4 part 2: live ML setup

Sitt wrote the `ml_setup` command with local sensor inputs and console and MQTT results, and checked it on one board on 19 November 2026. Hasif added the uploaded model (task 3.2), another board's sensor as input (task 3.3) and a local output that feeds this board's rules (task 3.4) on 22 November 2026. Those three are covered by the laptop tests in `tests/test_sensor_ml.c` (`sh tools/ml_test.sh`) and have not been run on a board. The task list is in [README section 5](../../README.md#5-d4-task-checklist).

## The command

Send one JSON message to `pico/<node>/cmd`. It names the model, the inputs in order, and where the result goes. The board applies a valid setup while the sensors keep sampling, with no `STOP`.

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

Each input is scaled to 0 to 1 between `min` and `max`. Indices run from zero with no gaps. The live path takes `sine-float` and `sine-int8` (one or more inputs, averaged into one), `autoencoder` (exactly 16 inputs), `comfort-summer` (temperature, then humidity) and `uploaded`. `people-counter` needs 31 inputs, more than the 16 a setup can bind, so it runs through `ML REPLAY` only.

**Uploaded model (task 3.2).** `"model":"uploaded"` picks the model last sent over MQTT into flash (D1 task 31); `ML MODEL uploaded` does the same from the console after `STOP`. Its input and output counts are read when it loads. It needs one input per model input, or any number averaged into a one-input model, and at most 16 outputs. If the inputs do not fit, the previous model is loaded again and the reply is `input_shape_mismatch`; with nothing uploaded it is `no_uploaded_model`. `ML LIST` shows whether a model has been uploaded. A new upload replaces the running model, so the ML stops with `MODEL_CHANGED` until `ml_setup` picks it again.

**Another board's sensor (task 3.3).** An input can name a sensor on another board: `"source":{"kind":"remote","node":"s3-001","name":"DISTANCE"}`, with the same `field`, `min` and `max`. This board must already follow that sensor with a `proto=mqtt` config that names its decoder:

```text
BEGINCFG|name=IN_DIST|proto=mqtt|mode=poll|freq_hz=2|remote_node=s3-001|remote_source=DISTANCE|remote_profile=pulse|ENDCFG
```

`remote_profile` is `aht`, `tsl2561` or `pulse`. It is Cody's decoder from the D6 branch, so a relayed reading decodes exactly as a local one. A sensor this board does not follow is refused with `remote_source_not_followed`, and a field its decoder does not give with `remote_field_not_decoded`. A local input may not name a followed sensor, so a remote reading never passes as a local one. Results say where their input came from: `"input_source":"sensors"`, `"remote:<node>"` or `"mixed"`. When the other board stops sending, its readings pass `max_age_ms` (`ML TIMING`) and the result is `MISSING_OR_STALE` instead of a value from old data. Switching between local and remote is one `ml_setup` each way.

**Where the result goes (task 3.4).** `{"kind":"publish"}` sends each result on `pico/<node>/ml/result` (QoS 1, not retained) and also prints it. `{"kind":"console"}` prints it only. `{"kind":"local"}` turns each result into a reading of a sensor called `ML` on this board, handled like any other reading: rules with `source=ML` act on it, an event-driven sensor can use `timing.trigger_source=ML`, and it is published on `pico/<node>/sensor/ML/line`, so another board can follow it with `remote_source=ML`. A class model (comfort-summer) gives its class, the comfort score, as `u16le(0)`; every other model gives output *i* times 1000 as `s16le(2*i)`. For example, this rule sets GPIO8 when a sine output is above 0.5:

```text
BEGINRULE|name=ML_HIGH|source=ML|calc=y=s16le(0)|when=y>500|action=gpio:8=HIGH|ENDRULE
```

`ML OUTPUT console`, `ML OUTPUT publish` and `ML OUTPUT local` change the destination while sampling, from the console or `pico/<node>/cmd`. Showing the result on the LCD is D6 task 5.5; the LCD board can follow `ML` or read `ml/result`.

**The reply.** The board answers on `pico/<node>/ml/status` (QoS 1, not retained) and on the console: `{"ok":true,"reason":"applied","generation":N}`, or `ok:false` with a reason and the generation unchanged. Each result carries the generation, so old and new results can be told apart. A successful setup clears the ML input cache, clears any `ML THRESHOLD` and waits for fresh readings. The command must be under 2,048 bytes. The console line is 512 bytes, so a 16-input setup goes over MQTT. Result payloads stay under MQTT's 512-byte limit. A published result is not proof that another board received it: a broker drop or a full queue can lose it.

## Apply and failure rules

The board parses the JSON and refuses unknown or repeated keys, wrong types, non-finite numbers, missing or repeated indices, a model and field that do not go together, `max` not above `min`, and an unknown output or source. It checks the model's shape and every input before changing anything. On success it switches the model, inputs and output together, increases the generation and waits for fresh readings; sensors and other rules carry on. On a refusal the previous model and inputs stay, and the reply has `ok:false`, the reason and the previous generation. If loading the new model fails, the previous one is loaded again and the reply says whether that worked.

The older `ML PRESET` and `ML MODEL` commands still need `STOP`. `ml_setup` and `ML OUTPUT` do not.

## Board check, 19 November 2026

Sitt ran this on one ESP32-S3 (node `sitt-s3-01`) with Mosquitto 2.1.4 on the laptop and no sensor attached; the AHT config was accepted only so the control path could run. Over MQTT, `ml_setup` accepted `sine-int8` at generation 2 and a live switch to `sine-float` at generation 3, and an unknown model was refused with the generation still 3. With `output: publish` at generation 7, `ML REPLAY 1.5707963` after `STOP` published a result with `input_source:"replay"`, `outputs:[0.9956719]`, `invoke_us:806` and `status:"OK"`. With `output: console` at generation 9 the same replay printed on the console and nothing arrived over MQTT in seven seconds. An unsupported output (at the time, `local`) and malformed JSON were each refused at generation 13, after which the previous `sine-float` model still published the same result and heartbeats continued. This showed MQTT control, model switching, refusal of bad setups and both result routes; it did not show a real sensor feeding the model.

## Laptop tests, 22 November 2026

`tests/test_sensor_ml.c` builds `sensor_ml.c` with the real measurement decoder and JSON parser; the scheduler, the output path and the model interpreter are stand-ins. For the uploaded model it checks that nothing can be picked before an upload, that the model's input and output counts are read on loading, that three inputs feed a three-input model without the sine scaling, and that a setup with the wrong number of inputs is refused with the previous model still running. For remote input it checks that only frames from the followed board feed the model, that this board's own sensor of the same name does not, that a stopped feed goes stale, that an unfollowed board, an undecoded field, a remote input without a node and a local input naming a followed sensor are all refused with the generation unchanged, and that switching back to local works. For local output it checks that a result becomes one `ML` frame with output times 1000 as `s16le`, that comfort-summer gives its score as the first byte, and that `ML OUTPUT` switches between local and console while sampling and refuses an unknown destination. `sh tools/ml_test.sh` runs these with the ML manager and REPL tests; all pass.

## Running it on boards

These steps have not been run yet. Use two boards on the same broker with different node IDs (`NODE=s3-002 sh tools/mtk.sh flash` for the second), and subscribe to `pico/#` on the laptop.

1. Board A (`s3-001`) gets the ultrasonic: `BEGINCFG|name=DISTANCE|proto=gpio|gpio.mode=pulse|mode=poll|freq_hz=2|gpio.trig=4|gpio.echo=5|gpio.trig_us=10|gpio.pulse_timeout_us=25000|ENDCFG`, then `RUN`.
2. Board B (`s3-002`) follows it with the `IN_DIST` config above, has a sensor of its own, and gets `RUN`.
3. Send board B an `ml_setup` with `sine-int8` on its own sensor, then a switch to `sine-float`, without `STOP`. Expect a higher generation and readings that never stop.
4. Send board B an `ml_setup` whose input is `{"kind":"remote","node":"s3-001","name":"DISTANCE"}`, field `distance_cm`, 2 to 400. Expect results with `"input_source":"remote:s3-001"` that follow your hand. Send `STOP` to board A and expect `MISSING_OR_STALE`. Switch back to the local input.
5. Add the `ML_HIGH` rule to board B and send `ML OUTPUT local`. Expect GPIO8 to follow the result, and board A to see it if it follows `ML` from board B.
6. Send an unknown model, a remote input from a board B does not follow, and malformed JSON. Expect `ok:false`, the same generation, and readings that carry on.
7. Upload a model with `project_v3/models/send_model.py`, then `ml_setup` with `"model":"uploaded"`. Expect `ML LIST` to show it and results with `"model":"uploaded"`.
