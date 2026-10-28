# Repository instructions

## Active target and branch

- D3 development uses the current `main` D1/D2 baseline and the ESP32-S3
  micro T-Kernel build in `esp32s3_mtk/`. Arduino/FreeRTOS is a retained
  comparison baseline, not the active application runtime.
- Inspect branch and working-tree state before editing. Preserve unrelated
  changes. Do not force-push or replace another contributor's work.
- Read `D3_TASKS.md`, `CODY_WEEK8_SUBMODULE.md`, the current README setup
  section, and relevant D2 handoff notes before changing integration contracts.

## Implementation boundaries

- Keep portable C logic and suitable large BSS state in
  `libraries/EdgeCore/src/`; keep micro T-Kernel task glue in
  `esp32s3_mtk/components/edge_mtk/runtime_mtk.c`.
- EdgeCore must not include Arduino, FreeRTOS, ESP-IDF, or kernel headers.
- Use micro T-Kernel objects in the active runtime. Producers must use bounded
  nonblocking message sends (`TMO_POL`), preserve drop counters, and check errors.
  If a shared-state mutex is necessary, create it with `TA_INHERIT`.
- Preserve ApplicationTask on core 1, priority 23, with its existing 4 KiB stack
  unless measurements justify a change. ApplicationTask owns occupancy updates;
  send reset requests through its FIFO rather than mutating state from another task.
- Size typed message buffers and receive objects from the actual message type.
  The earlier 64-byte application item cannot carry the 80-byte EventRecord.
- Internal RAM is tight. Avoid large new internal static buffers or unchecked
  stack increases. EdgeCore BSS is mapped to PSRAM by the active linker fragment;
  verify actual placement and stack/heap headroom in target evidence.
- Preserve the course port's internal-RAM placement for kernel message buffers
  and task stacks. Account explicitly for any typed-buffer growth.
- Preserve existing SPI diagnostic and benchmark command dispatch.

## Ownership and evidence

- Cody owns D3 timing/application contracts, application integration and
  acceptance interpretation. Coordinate sensor work with Sitt, scheduling with
  Brendan, platform/MQTT with Hasif, and inference with Jun Yong.
- Brendan/Jun Yong own D2 storage. Agree identity/result/reason mappings using
  `D2_EVENT_RECORD_DESIGN.md` before adding application flash logging. Attempt
  publication enqueue before logging; enqueue success is not broker delivery.
- No flash work in SchedulerTask. Do not erase or repeat one-time scratch writes
  as part of ordinary tests. Keep synthetic events distinct from real entrance evidence.
- Do not invent or freeze node/boot identity, cross-node time, persistence,
  observation-window or numerical timing policies without the agreed contract.
- Keep known review defects and unrun hardware gates explicit. A successful
  merge, native test, or source-purity scan is not hardware acceptance.

## Verification and delivery

- For application changes run `sh tools/cody_test.sh` and the relevant native
  runtime tests, `sh tools/d2_host_test.sh`, `sh tools/check_hal_boundary.sh`,
  and `sh tools/mtk.sh purity`.
- With the pinned SDK, build and run `sh tools/mtk.sh image`. On a connected board
  run `test 4a`, `test 5-neg`, `mqtt <broker-ip>`, application cases and `TASKS`.
  ApplicationTask must execute only on core 1. Inspect stack, heap and drop counts.
- Add meaningful regression coverage for changed behavior. Record which checks
  actually ran and which need the SDK, wiring, broker or target board.
- Keep README, D3 checklist and feedback notes accurate. Retain current main's
  D1/D2 evidence; do not mark reviewed timing/identity defects or D2 T-23 closed
  merely because the task glue is ported.
- Commit/push only within the user's requested delivery scope; never include
  credentials, generated firmware or local SDK/build output.
