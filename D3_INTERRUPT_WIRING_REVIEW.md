# D3 GPIO interrupt and resistor review

Review date: 5 November 2026. Source checkpoint: `aaf48da`.
Status: documentation/source review and native validation; physical acceptance pending.
No firmware, wiring or resistor removal has been performed or approved by this review.

## Alignment with README D3 task list

Task IDs below follow [README section 4](docs/MAIN_BRANCH_README.md#4-d3-task-checklist).
The local `D3_TASKS.md` uses an older, longer numbering scheme; its D3-01 is
not README's D3-1. Use the README IDs for this review and acceptance reporting.

| README task | Review contribution and current acceptance boundary |
|---|---|
| D3-1: timing modes/configuration | Existing parser, timing and scheduler regressions rerun successfully; detailed evidence below. Active-window runtime-change policy remains open as recorded in README. |
| D3-2: async HC-SR04 | Source/lifecycle review and native coverage; physical capture, missing-Echo and STOP/CLEAR acceptance pending. |
| D3-3: 50 ms logical pair | Existing native order/guard/deadline tests pass; actual PAIR and pin/voltage verification pending. |
| D3-4: radar window | Existing native window/integration tests pass; physical radar/sonar crossings pending. |
| D3-5: order and acoustic guard | Conditional wiring assessment only; no guard value selected and no crosstalk result claimed. |
| D3-6: requested/measured timing | Board interval table and hardware mode demonstrations pending; native clock values are not measured sensor timing. |
| D3-7: entrance workload timing | Report/counter path covered natively; real workload WCET and timing margins pending. |
| D3-8: ApplicationTask | Existing native glue/identity suites pass; board app-test, ABI and TASKS resource evidence pending. |
| D3-9: explicit faults | Native timeout/busy/configuration/queue coverage passes; electrical noise and physical fault acceptance pending. |
| D3-10: D1 regression | Native D2 and purity checks rerun; exact-image D1 board suite is still required. |
| D3-11: open contracts | Preserve approved window/identity policy; cross-node time, occupancy persistence and D2 mapping remain open. |
| D3-12: documentation/review | This review, file-impact register and retained native evidence are ready for review; no clean-clone target build, merge or team sign-off claimed. |

### D3-1 execution record — 5 November 2026

Executed the documented `sh tools/cody_test.sh` entrypoint against checkpoint
`aaf48da98c9824b0c262ac197486dfa1c740b507`, with documentation changes only.
It compiles the real parser/timing/scheduler modules with native hardware seams.
All suites exited successfully; this execution adds evidence, not new firmware
behavior or new test cases.

| D3-1 requirement | Existing test evidence | Result |
|---|---|---|
| Periodic state and anchored scheduling | `test_cody_contracts.c::test_timing`; `test_scheduler_timing.c::test_periodic_timeout_recovers` and `test_periodic_skip_and_invalid_config` | PASS: scheduled due times, recovery and no catch-up burst in covered cases. |
| One-shot and explicit rearm | `test_scheduler_timing.c::test_one_shot` and `test_clear_while_running` | PASS: once per arm, explicit trigger, reset/reconfigure behavior. |
| Event-driven source and bounded execution | `test_cody_contracts.c::test_timing`; `test_scheduler_timing.c::test_event_source_and_timeout` | PASS: source handoff, duplicate arm refusal, timeout and rearm. |
| Timing keys and invalid combinations | `test_cody_contracts.c::test_config_and_sink`; `test_scheduler_timing.c::test_periodic_skip_and_invalid_config` | PASS: parsed mode/window/timeout/source, timeout exceeding window refused, invalid interval refused before driver setup. |
| Enabled flag and deadline semantics | `test_scheduler_timing.c::test_disabled_stream`, `test_acquisition_deadline` and `test_pending_acquisition` | PASS: disabled stream stays inactive, exact-deadline completion accepted, late samples suppressed and pending acquisition cancelled. |
| Shared Trigger/Echo rejection | `test_gpio_pulse_config.c::test_parser` and `test_driver` | PASS: identical pins refused, direct-driver bypass protected, distinct pins and ordinary digital input accepted. |
| Cross-sensor ownership before hardware mutation | `test_gpio_pulse_config.c::test_scheduler_with_real_gpio` | PASS: conflicting GPIO/pulse/SPI assignments refused without new mode calls; installed sensors preserved. |

Full output, commands, source and exit results are retained in
[D3-1 native evidence](parity/logs/d3/d3-1-native-20261105.log).
The same run also passes the other D3 suites (twelve native C suites and twelve
Python tests in total). Additional commands `sh tools/d2_host_test.sh` and
`sh tools/mtk.sh purity` both exit 0. Purity covers 71 platform files and the
shared HAL boundary; linked-image validation is skipped because no built ELF
exists. Two existing nested-comment compiler warnings remain.

D3-1 therefore retains README's **Done (laptop tests)** status for the covered
contract. This is not exhaustive proof of every possible configuration. The
active-window runtime-reconfiguration policy remains an explicit open contract;
observed cancellation behavior alone does not close that policy decision.
No board commands, flash operations or wiring changes were performed.

## Merge update — 5 November 2026

The incoming main branch (`b496a8d`) adds a recorded three-sensor breadboard:
AHT/TSL2561 share GPIO1/2 I²C, and a documented 3.3 V-compatible HC-SR04+
uses Trig GPIO4/Echo GPIO5. See the [current root README](README.md),
[active wiring](esp32s3_mtk/WIRING.md) and [preset](config/three_sensors.cfg).
The [framework D3 checklist](docs/MAIN_BRANCH_README.md#4-d3-task-checklist)
and earlier regression guide are preserved under `docs/MAIN_BRANCH_README.md`.

The review and D3-1 execution record below describe the pre-merge `aaf48da`
checkpoint and alternative test fixtures. GPIO1/2 sonar and GPIO5/6 loopback
must not be applied to the incoming breadboard. The new documentation identifies
its own compatible module; it does not establish the exact circuit or measured
voltages on this checkout's board. No circuit or firmware modification is made
as a resistor-removal action. The incoming optional sensor profiles and startup
fix are preserved as upstream integration changes. Dual-sonar pins/guard and
physical 50 ms acceptance remain unverified.

Post-merge native checks pass: D3 contracts/runtime, D2 host, sensor profiles/
scheduler integration, startup lifecycle, six sensor-loader tests (repository
venv) and source/HAL purity (75 platform files). [Merge test evidence](parity/logs/d3/merge-main-20261105.log)
records the parent commits and output. No target build, image or board acceptance
was performed; linked-image validation was skipped without a built ELF.

## Decision and evidence boundary

For the pre-merge standalone resistor-free tests, the recorded fixture is README section 5,
Part E's kit 3 V sonar: VCC at 3V3, common ground, Trig GPIO1, Echo GPIO2.
This is a recorded baseline, not a measurement of the currently fitted hardware.
The exact model/revision, circuit, supply and Echo voltage have not been supplied
for either sonar. No particular installed resistor is cleared for removal yet.
The second sonar and full entrance harness have no verified pin allocation here.

Keep the documented Echo divider or suitable level shifter on a standard 5 V
HC-SR04. Internal pull resistors cannot provide voltage-level protection.
The previous conversational statement that our tests need no resistors applies
only to the documented compatible 3 V fixture and wiring-free tests.

## Baseline documents and discrepancies

Reviewed root README sections 4, 5 Parts E/G and 6, SETUP's pin/power and
µT-Kernel hardware-test sections, ARCHITECTURE's D3 updates and hardware table,
MTK_REDESIGN's interrupt description, the GPIO configuration guide in
`project_v3/README.md`, and the historical `esp32s3/README.md` bring-up guide.
At that checkpoint there was no application README directly under `esp32s3_mtk/`; the incoming merge adds one with the current fixture.

| Evidence | Recorded baseline / discrepancy |
|---|---|
| README Part E and section 6 sonar row | Kit 3 V module: GPIO1/2, direct wiring; explicitly distinguishes a 5 V module needing Echo division. |
| README Part E, “Why no resistor” | Starts with the 3 V kit but ends by saying HC-SR04 needs division because it is powered at 5 V. This must be qualified by module type. Powering a board at 3.3 V alone is not proof that every interface is compatible. |
| SETUP pin/power diagram | Older 5 V sonar on Trig GPIO4/Echo GPIO7 with 10 kΩ upper and 20 kΩ lower divider. This is a different fixture from the current single-sonar acceptance fixture. |
| SETUP hardware-test rows 9–10 | Agrees with README's kit 3 V direct GPIO1/2 fixture, with a separate 5 V alternative. |
| ARCHITECTURE section 5 | Describes a 5 V sonar/divider and Pico-oriented GP16/17 assignments; do not copy those numbers onto the Super Mini acceptance fixture. |
| ARCHITECTURE dated D3 updates | The older 30 October paragraph says no sequence/window producer; later updates and README describe both. Read the chronology. |
| `tools/mtk_live.py` introductory text | Says Echo GPIO2 “through a divider” without distinguishing the kit fixture. Its executable config uses GPIO1/2 with no pull setting. |
| `project_v3/README.md` GPIO key table | Shows numeric pull values. Verified `config_parser.c` accepts `up`/`down` and otherwise sets `off`; it does not normalize numeric values. `gpio.c` uses those strings. Use the active README string examples; the numeric guide needs correction. |
| README section 4 and Part G | D3 board capture, cancellation, PAIR, crosstalk and workload timing remain pending. Older real-sonar results are not acceptance of the current async firmware. |

No baseline wiring diagram has been silently replaced. Once the exact fixture
is validated, reconcile these documents and helper comments together.

`D2_D4_RESEARCH_CONTEXT.md` is dated 26 September 2026 and records an earlier
research proposal: sonar Trig GPIO2/Echo GPIO13 through level conversion, a
different TFT pin allocation, and a Pico-only repository baseline. These are
historical assumptions, not the active µT-Kernel fixture. In particular, its
provisional Echo GPIO13 overlaps the current D2 MISO fixture. Do not combine
that allocation with README's present D2/D3 wiring. Preserve the dated research
history and add a clearly dated supersession note when reconciling it.

## Files affected and proposed follow-up scope

This review changes documentation only. Removing a verified redundant external
resistor does not itself require changing interrupt code. The firmware rows
below identify impact boundaries if measurements later justify a software change;
they are not instructions or approval to edit those files now.

| File | Impact / proposed action | When |
|---|---|---|
| [D3_INTERRUPT_WIRING_REVIEW.md](D3_INTERRUPT_WIRING_REVIEW.md) | Review findings, electrical evidence, conditional wiring comparison, resistor decisions, pin map, test plan and file-impact register. | Created for this review; updated with this register. |
| [parity/logs/d3/d3-1-native-20261105.log](parity/logs/d3/d3-1-native-20261105.log) | Full native D3-1 execution and companion D2/purity output with source/platform provenance. | Added for this validation; no board evidence implied. |
| [AGENTS.md](AGENTS.md) | README-first and hardware-verification guardrail. | Already updated locally; ignored and must not be staged. |
| [README.md](README.md) | Added the D3-1 native recheck and links to this review/raw output. Still needs fixture reconciliation in Part E and section 6 once hardware is verified. | Evidence note added now; no baseline wiring change made here. |
| [SETUP.md](SETUP.md) | Clearly label GPIO4/7 5 V divider fixture versus GPIO1/2 kit fixture; record actual A/B/radar harness, supply, resistor values and measured levels. | After actual hardware is verified and tested. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Align the current hardware diagram and active async interrupt description; retain dated baseline history. | After validated fixture and implementation evidence are available. |
| [D2_D4_RESEARCH_CONTEXT.md](D2_D4_RESEARCH_CONTEXT.md) | Add a dated note that its Pico-only baseline and provisional sonar/TFT allocation are historical; link to current README and this review. Preserve its original research context. | Documentation reconciliation follow-up; currently unchanged. |
| [project_v3/README.md](project_v3/README.md) | Check numeric-versus-string pull examples against parser behavior; clarify platform-specific async behavior if needed. | Documentation correction after source verification; no Pico wiring change implied. |
| [tools/mtk_live.py](tools/mtk_live.py) | Qualify the introductory “through a divider” wiring comment by module type. Its executable GPIO1/2 configuration needs no change for the documented compatible fixture. | Comment/help update when fixture documentation is reconciled. |
| [parity/PARITY.md](parity/PARITY.md) | Add exact-commit D3 capture, cancellation, PAIR and regression outcomes with links to raw logs; distinguish historical results. | After target tests, never from native results alone. |
| [parity/logs/mtk/d3/](parity/logs/mtk/d3/) | Store new physical test logs, fixture metadata, scope traces and window reports; identify board, commit, image/tool versions, tester and date. | During future validated board runs; existing logs untouched. |
| [hal_mtk.c](esp32s3_mtk/components/edge_mtk/hal_mtk.c) | ISR registration, edge selection, internal pull configuration and timestamp handling. No change required by the present review. | Only if measured electrical/timing faults justify an approved implementation change. |
| [pulse_capture_mtk.h](esp32s3_mtk/components/edge_mtk/pulse_capture_mtk.h) | Core ownership, start/poll/cancel, timeout and shared timestamps. Preserve the same-core capture contract. | Conditional software impact only; currently unchanged. |
| [gpio.c](libraries/EdgeCore/src/bus/gpio.c) | Echo input/pull configuration, async acquisition and cleanup. Preserve explicit errors and cancellation before free. | Only if validated input-bias/lifecycle requirements require code changes. |
| [pulse_sequence.c](libraries/EdgeCore/src/application/pulse_sequence.c) | Pair ordering, explicit guard and complete 50 ms budget. External resistor omission alone does not affect these semantics. | Only if a measured sequencing defect is established. |
| [entrance_window.c](libraries/EdgeCore/src/application/entrance_window.c) | Radar window, pair results, fault/ambiguity and timing counters. Preserve occupancy safety. | Only if a validated window defect is established. |
| [scheduler.c](libraries/EdgeCore/src/application/scheduler.c) | Configuration ownership/conflicts, capture servicing, STOP/CLEAR and PAIR/WINDOW integration. | Conditional integration impact if a firmware change is needed. |
| [runtime_mtk.c](esp32s3_mtk/components/edge_mtk/runtime_mtk.c) | SchedulerTask core ownership, HAL initialization and console delivery. No task/core/priority change is proposed. | Only if evidence requires runtime integration changes. |
| [parity/parity_test.py](parity/parity_test.py) | Current board pin translation and `t06`, `4c-sonic`, `sonic-rule` assertions. Existing stages remain the baseline. | Change only if a verified fixture or newly required assertion needs it; retain existing coverage. |
| [tests/test_mtk_pulse_capture.c](tests/test_mtk_pulse_capture.c), [tests/test_gpio_pulse_config.c](tests/test_gpio_pulse_config.c) | Native capture and driver lifecycle regression coverage. | Update only alongside a relevant behavior change; cannot validate voltages. |
| [tests/test_pulse_sequence.c](tests/test_pulse_sequence.c), [tests/test_entrance_window.c](tests/test_entrance_window.c), [tests/test_scheduler_timing.c](tests/test_scheduler_timing.c) | Native order, guard, deadlines, faults and scheduler regression coverage. | Update only if related behavior changes. |

No D2 flash codec, D4 model, NVS identity, Pico/Arduino backend or course-kernel
modification is proposed. D2 wiring is a conflict constraint and D4 activity is
part of workload regression, not grounds to expand this resistor review.

The local `skills.md`, `.agents/` and planning notes remain local and unstaged.
No changes were made to `D2_D4_RESEARCH_CONTEXT.md`, firmware, test code or wiring
while adding this file-impact register.

## Electrical basis

Espressif's [ESP32-S3 datasheet, section 5.4](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
specifies VIH minimum 0.75 × VDD, VIL maximum 0.25 × VDD, high-input maximum
VDD + 0.3 V, and typical 45 kΩ weak internal pulls. At 3.3 V, the logic thresholds
are 2.475 V and 0.825 V. Design for 3.3 V logic; the input ceiling is not permission
to connect 5 V. Supply-pin absolute maxima are not GPIO operating specifications.

The [ElecFreaks HC-SR04 datasheet](https://www.elecfreaks.com/download/HC-SR04.pdf)
specifies a 5 V supply, TTL Echo and a 10 µs trigger, with more than 60 ms between
measurements recommended. It does not establish 3.3 V-safe Echo for every clone.
The [HC-SR04-33 datasheet](https://cdn.sparkfun.com/assets/a/8/9/c/a/HC-SR04-33_Datasheet.pdf)
specifies 3.3–5 V operation but gives no numerical Echo VOH/VOL guarantee.
Neither document identifies our fitted kit modules. Match the actual model and
verify levels, including startup and overshoot, before connecting unprotected Echo.

A nominal 1:2 divider produces 5 × 2/3 = 3.33 V. Check supply maximum,
resistor tolerances, receiver rail and waveform overshoot; this nominal arithmetic
alone is not final electrical acceptance. A lone series resistor does not establish
safe voltage at a high-impedance input. A 1:2 divider on a 3.3 V Echo would produce
about 2.2 V, below the S3's guaranteed high threshold. This explains why retaining
an unnecessary divider can also cause missed readings, but does not authorize removal.

## Resistor decisions and before/after comparison

“Removable” below is conditional on verified module/circuit evidence and successful
tests. Unknown installed components remain in place until their purpose is established.

| Connection/component | Classification | Before → conditional outcome | Reason / risk |
|---|---|---|---|
| Standard 5 V sonar Echo divider (both branches), or equivalent level shifter | Required | Protected Echo → retain protection | Removing either branch defeats the intended ratio; direct 5 V can damage GPIO. |
| Divider on a verified 3.3 V-compatible, 3.3 V-powered sonar Echo | Removable after verification | Echo → divider → input → direct Echo → input | Only if measured/specification levels are safe; an unnecessary divider may attenuate a valid high below VIH. Wrong module identification risks damage. |
| Extra external Echo pull on a verified push-pull output | Optional; removal needs circuit/noise evidence | Biased input → driven input, with a deliberate idle-state policy | Not needed merely because capture uses interrupts. Floating/disconnected input and long wires can create false edges. |
| Bias for disconnected Echo in `t06` | Required bias function; external resistor removable when internal pull is adequate | External pulldown → existing `gpio.pull=down` | Test already selects internal pulldown. It provides idle bias, not voltage conversion. |
| Trigger series resistor | Optional if no protection/termination function is required | Existing series connection → documented direct trigger | Push-pull GPIO drives a logic input, not the acoustic transducer. Removal without circuit verification could worsen ringing or contention. |
| GPIO5→GPIO6 3.3 V loopback bias resistor | Removable only for the verified output-to-input fixture | Output → resistor/jumper → input → documented jumper | Receiver internal pulldown already used in tests. Never join two configured outputs; current limiting may be intentional in another fixture. |
| Open-drain counter/switch pull-up or pull-down | Required bias; internal substitution conditional | External bias → retain unless timing/noise specs support internal pull | Weak internal bias may be too slow for cable capacitance or fast edges. |
| LED/load current limiter, board boot straps, I2C pull-ups, module protection | Required unless separately proven unnecessary | Retain → retain | Not removed as part of GPIO interrupt simplification. Internal GPIO bias does not replace their electrical functions. |

## Pin map: documented fixtures, not an installed-harness claim

| Fixture/signal | ESP32-S3 GPIO / connection | Mode and interrupt |
|---|---|---|
| README single kit sonar Trig | GPIO1; VCC 3V3, common GND | Push-pull output, default 10 µs high; no Trig interrupt |
| README single kit sonar Echo | GPIO2 direct only for verified compatible module | Input; rising then falling edge; default pull off in `4c-sonic` and `live sonic` |
| Missing-sensor `t06` Trig/Echo | GPIO4/GPIO7; nothing connected | Trig output, Echo input with internal pulldown |
| `t06` absent one-wire input | GPIO6 | Input pull-up; software read, not sonar interrupt capture |
| Older SETUP 5 V sonar | Trig GPIO4; Echo GPIO7 through documented divider | Separate alternative fixture; do not combine with `t06` |
| Counter loopback | GPIO5 output → GPIO6 input | Input internal pulldown, any-edge interrupt in counter mode |
| Second sonar A/B installation | Not verified; configuration chooses distinct Trig/Echo pins | Separate inputs; one shared capture slot, sequenced transmitters |
| LD2450 entrance radar | UART pins chosen by verified configuration; not established here | UART receive interrupt, not sonar GPIO edge capture |
| USB and D2 constraints | Reserve GPIO19/20 for USB; D2 uses GPIO10–13 | Do not repurpose connected flash/PSRAM, strap or active peripheral pins |

`parity/parity_test.py` maps Pico 16→S3 1, 17→2, 18→7, 20→4, 21→5,
22→6. Configuration controls interrupt pins; the ISR does not hard-code GPIO2.
HAL pin validity is not a complete check of exposed board pins, boot straps or
the installed harness. Verify those independently before choosing the second pair.

## Implementation review

- `hal_mtk.c`: `hal_mtk_init()` creates the Echo event flag and registers
  `gpio_isr` using `tk_def_int(ETS_GPIO_INTR_SOURCE, TA_HLNG)` and `EnableInt`.
  `runtime_mtk.c` calls initialization in SchedulerTask, assigned core 1.
  The pinned port's `kernel/tkernel/int.c` delegates to `knl_define_inthdr` in
  `kernel/sysdepend/cpu/core/xtensa_lx7/interrupt.c`, which routes the peripheral
  to the calling CPU. This uses the course registration path.
- `gpio_isr` reads/clears pending status, increments enabled counter pins,
  records rise/fall timestamps, changes edge selection and sets `ECHO_RISE` or
  `ECHO_FALL` through `tk_set_flg`. No waits, heap allocation, logging or
  application classification are in the normal handler. The optional test hook
  must remain bounded and normally NULL. The pinned event-flag implementation
  permits set from handler context; it protects its kernel state internally.
- These are software timestamps sampled once at ISR entry via
  `esp_timer_get_time()`, not hardware-latched edge timestamps. ISR latency,
  GPIO status coalescing and changing edge selection can lose very short/noisy
  pulses. Native tests do not bound these effects or prove zero lost edges.
- `pulse_capture_mtk.h`: start refuses a busy/shared resource, a high Echo,
  invalid pins/timing and callers outside the owner core. It arms positive edge
  before triggering. The first ISR records rising time and switches to negative
  edge; the second records falling time and disables that Echo interrupt.
- Poll masks Echo interrupt delivery while copying the volatile 64-bit timestamps
  on the 32-bit CPU, validates order/deadline and cancels on success or timeout.
  This protection relies on same-core, single-owner access, not `volatile` alone.
  Cross-core capture use is refused. Do not generalize it into a cross-core API.
  Target stress must still exercise edges coincident with poll/cancel/rearm.
- Cancel disables Echo, clears its status, resets ownership and drives Trig low.
  `gpio.c` deinit cancels before freeing context. Scheduler reset stops WINDOW
  and PAIR before releasing sensors; STOP cancels pending driver work and the
  active window. Runtime console requests are processed by SchedulerTask.
- The async path polls completion at bounded scheduler wakeups; it does not wait
  on Echo flags. Flags also support the historical synchronous HAL path. Other
  synchronous drivers may delay scheduler service. Trigger generation still
  briefly spins (default 10 µs, allowed up to 1000 µs); the Echo wait is async.
- `pulse_sequence.c` validates timeout A + guard + timeout B against a maximum
  50,000 µs window, uses timestamps rather than sleeps, releases capture before
  the next transmitter, and starts guard after consuming the first outcome.
  Late task service cannot shorten guard; it may instead fail the pair deadline.
- `entrance_window.c` runs finite radar-gated pairs, rejects essential faults and
  ambiguity, tracks timing outcomes and cancels the pair on closure/STOP.
  A nominal 100 ms period and per-sonar retrigger checks do not establish
  physical crosstalk clearance. Guard remains explicitly supplied and measured.

Interrupt operation is unchanged by omitting an electrically redundant resistor:
the input still must cross valid low/high thresholds, then the same rise/fall
handler supplies width to SchedulerTask. No firmware change is presently justified.
For disconnected-input testing use the existing explicit pulldown configuration.
Do not add pulldown blindly to a divider: it loads the lower branch and changes
both voltage ratio and edge behavior.

## Validation and remaining gates

Native checks rerun for this review: `tools/cody_test.sh` PASS (twelve native C
suites plus twelve Python tests), `tools/d2_host_test.sh` PASS, and
`tools/mtk.sh purity` PASS for 71 platform files and the shared HAL boundary.
Image validation was skipped because no built ELF is present. Two existing
nested-comment warnings in `mqtt_model_transfer.h` remain. Native logs are in
`/tmp/d3-interrupt-review-native.log` and `/tmp/d3-interrupt-review-d2.log` for
this session. Native capture tests execute the actual capture functions with
mocked register/clock boundaries, not real pad voltages or ISR latency.

No board commands were run: exact installed wiring/voltages are unverified,
and the user explicitly requires verification before firmware or wiring changes.
Existing `parity/logs/mtk/4c-sonic.log`, `sonic-rule.log`, `t06.log` and
`29-echo-and-wcet.txt` are historical evidence only.

After exact module/circuit verification, approval of any removal, and safe
powered-off wiring work, validate on the pinned D3 image in this order:

| Test | Acceptance / retained evidence |
|---|---|
| Electrical fixture | Model/revision, pin-labelled circuit, resistor values, supply range and scope measurements of Echo high/low/overshoot at the module and MCU input; verify idle, startup and missing-sensor states. |
| `sh tools/mtk.sh test t06` | Nothing connected, 7/7; explicit missing-Echo timeout, no valid-looking width, console still responds. |
| Single sonar / `live sonic`, `test 4c-sonic` | README GPIO1/2 fixture; plausible widths/distances, 4/4; compare scope rise/fall separation with reported width and count missing/false captures. |
| `sh tools/mtk.sh test sonic-rule` | 7/7; move object across 30 cm, preserve rule behavior. |
| STOP/CLEAR while Echo pending | Capture disabled, Trig low, no stale sample publication after cancellation; immediately reconfigure/rearm and repeat without resource growth. Include missing rise and stuck-high/missing fall with a verified 3.3 V stimulus. |
| Dual-sonar PAIR | Verified distinct pins and individually safe Echo circuits; explicit measured guard and timeouts fitting 50 ms. Exercise both orders, success, each missing Echo and cancellation; verify actual Trig/Echo traces, no overlap, losses or false widths. Two default 25 ms timeouts leave no positive guard budget. |
| Real WINDOW workload | Entry, exit, turnaround and faults; retain window-report counters, p95/p99 bounds, max, skipped/deadline counts and scope traces. No occupancy change on ambiguity or essential faults. |
| Full target regression | Pinned build/image, 4a, 5-neg, MQTT, app-test and TASKS core/stack/heap; compare timing under Wi-Fi/other sensors, not just isolated captures. |

For every future removal, retain a row containing baseline README reference,
photographed/traced existing circuit, proposed connection, matching module spec,
voltage/waveform evidence, affected source files, before/after test results and
failure risk. Until then the wiring tables above remain conditional review
material. Update README, SETUP and architecture diagrams with the same validated
fixture and evidence, without replacing historical results with unmeasured claims.
