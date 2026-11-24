# D6 remaining work (24 November 2026)

This is the current implementation checklist for the three-board, sonar-only use case. See [the use case](SONAR_ONLY_USECASE.md) for the crossing rules and the [model-by-model ML commands](../../ML_commands.md) for live configurations, replay files, and switching. Status is based on code and recorded tests; a build or host test is not a physical doorway validation.

| Work | Current state | Next acceptance check |
| --- | --- | --- |
| Board A sensing | AHT, light and each sonar have produced live values; intermittent sonar timeouts remain | Fixed-target readings and both-sonar sequential capture on the final wiring |
| Two-sonar doorway logic | Implemented and flashed on A; opt-in `DOORWAY` session | Real entry, exit, walk-by, turnaround, invalid echo and recovery trials; tune per-sonar near/clear thresholds |
| Five bundled ML models | Model switching, replay and some live tests exist | Record results for all five on the current three-board setup, distinguishing replay from real sensor input |
| `people-counter` 31-value input | A local rolling window and B's one-binding remote rolling window are implemented and flashed; the new B path is not yet hardware-tested | Compare 31-sample frames with the source training cadence and preprocessing, then validate classes against labelled crossings; never count every overlapping classification as a new person |
| B-owned occupancy | Pending; A's `DOORWAY` currently reports local session totals | A sends bounded crossing candidates with identity/sequence/quality; B validates, deduplicates and alone changes occupancy |
| B-to-C live display | C accepts separate sonar distances; a laptop relay currently publishes the full snapshot, while B publishes comfort separately | B publishes its own fresh AHT, light, both sonar values, occupancy, selected ML result and status; C displays them without the relay |
| Model-specific LCD rules | Pending beyond the current comfort card | Show comfort 80–99 as COMFORTABLE; scalar sine `>0.5` as CUT OFF and `<=0.5` as AT OR BELOW CUT OFF; people-counter IN/NONE/OUT with separate confidence; agree an autoencoder view for 16 outputs |
| Stale data | Comfort score expires on C; the complete snapshot lacks per-field expiry | Mark each stale/missing source and the overall display status accurately when A or B stops |
| Cold start | A/B configuration and C `D6VIEW` still need manual commands | Power-cycle all three with only the broker running; verify automatic A sampling, B processing and C display |
| SPI flash persistence | Board B SPI identification passed; live D6 logging contract and writes remain open | Agree record format, persist accepted decisions/readings and verify restart recovery |

The `people-counter` window is a model-specific exception to the 16-binding configuration limit: **one sonar binding fills 31 time slots**. The sine models, comfort model and autoencoder keep their original input shapes. B's current Sonar A feed is 2 Hz; the source people-counter model was trained around 50 ms per sample. Its B-side output is experimental until a matching sampling/preprocessing plan and labelled crossing test are complete.
