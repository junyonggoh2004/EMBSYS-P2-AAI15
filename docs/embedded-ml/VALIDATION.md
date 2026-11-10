# Embedded ML validation — 3 November 2026



Branch: `dev-jy-ml`. Base: `b496a8d`. This records the ML implementation; it is separate from historical sensor-only target evidence.



| Check | Result |

|---|---|

| ESP32-S3 firmware compile/link using `tools/build.ps1 build` | PASS |

| Image | 1,298,528 bytes (`0x13d060`), fits existing `0x2f0000` app partition |

| Image SHA-256 | `4c9181b70bb2cfad82e2c122b93b5bc26acb3c01eefe6af886745b021d519482` |

| Link/source audit | PASS; µT-Kernel SMP, USB and PSRAM; original FreeRTOS scheduler excluded; ML command/poll/decoder linked and sample sink is a strong ELF symbol |

| Actual ML module + actual numeric decoder, simulated inference boundary | PASS; four-field reduction, valid zero lux, cross-bus binding replacement, float/int8/vector selection, missing/stale/skew handling, invalid fields/ranges/layout, recovery and clock wrap |

| Actual inference manager, simulated TFLite boundary | PASS; exact input counts, finite values, model revision and stale output invalidation |

| ML serial loader tests | PASS, 6 tests; exact acknowledgements, refusal, timeout, configuration order, AHT/light-only selection |

| Existing three-sensor driver/scheduler tests | PASS; interleaving, faults, recovery, STOP/CLEAR and raw compatibility |

| Existing D3 framework regression suite | PASS, including native tests of actual runtime task bodies with modeled kernel boundaries |

| Real desktop LiteRT inference | PASS; both upstream sine models and existing 16-input autoencoder allocate and return finite outputs |

| Target model numerical agreement, resource use and stability | NOT YET TESTED |

| Physical sensor swaps/failure/recovery with ML enabled | NOT YET TESTED |



Desktop tests use `ai-edge-litert 2.3.0`. [Reference outputs](reference_outputs.json) are actual desktop model results; they are not board captures. The C pipeline tests simulate interpreter results to isolate dataflow/lifecycle and must not be presented as actual neural-network inference.



For example, desktop input 1.829175 produces 0.95309836 from sine-float and 0.95346002 from sine-int8. An ESP32 capture must record its actual input and compare against that same model, rather than compare two asynchronously sampled sensor readings. Initial suggested absolute tolerances are 0.0001 for the float model and 0.02 for the quantized sine model; investigate mismatches before accepting them.



Reproduce host checks from the repository root:



```sh

sh tools/ml_test.sh

sh tools/sensor_test.sh

sh tools/cody_test.sh

python -m unittest discover -s tests -p test_ml_loader.py

python tools/ml_reference.py

```



Use a C11 compiler with math library support. On this Windows machine the tests ran with MSYS2 UCRT GCC and Git Bash; Python loader tests ran in the existing ESP-IDF environment. The desktop reference tool ran in separate tooling dependencies, without changing the firmware's pinned SDK/runtime.



The audit checks `sensor_measurement_notify` is strongly provided by the ML platform module; the default shared weak no-op is used on other targets. The runtime test fixture supplies ML-boundary stubs because its existing tests concern control/application tasks; the dedicated ML tests cover the actual ML module separately.



Firmware build retains inherited SDK/kernel warnings. LCD is disabled. The implementation was validated locally without automated flashing, resetting or board configuration. Git publication does not establish hardware execution. Follow [RUNNING.md](RUNNING.md) for the board checks.



## Scope remaining from the plan



The current supported adapters are AHT/TSL2561 I²C and GPIO pulse distance. Runtime selection covers three immutable bundled models; new arbitrary model packages, runtime manifest validation/persistence and serial file upload are not implemented. Both sine models have pinned upstream provenance, checksums and Apache-2.0 attribution in `models/hello_world/manifest.json` and LICENSE. The vector model reuses existing repository bytes; its original training provenance is not newly established.



Model selection disables inference, clears cached data and attempts rollback to the previous bundled bytes if loading fails. This is not a power-loss persistence test. The new sensor/ML configuration is intentionally volatile. Local console results do not introduce new MQTT result topics.


Console follow-up: the supplied board capture shows repeated scheduler debug output and rejected manually edited commands. Backspace/DEL editing and whitespace trimming now pass tests of actual repl_line_feed; scheduler debug is disabled on the ESP32 target. The AHT/light-only loader avoids manual typing and distance timeouts. Firmware rebuild and linked audit pass. Updated ML invocation on board is still pending.
