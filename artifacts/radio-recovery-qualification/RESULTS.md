# Radio replacement qualification — PASS within stated scope

## Observed

Fresh Linux ARM64 VM `clab-vrt-radio-recovery-20260925-235042`, Containerlab 0.79.0, pinned patched VRT headers, application image `sha256:0ad20b0b136e95c8316c66c93224d4793b74e68f79b33da0513d358cf6cbdb78`. Source commit and archive hash are in session.json; inherited working-tree changes are preserved in source-changes.patch.

- Initial full synthetic lab traffic passed. All four measured tones were within half an FFT bin (244.140625 Hz) of configured frequencies.
- Native filtered destruction of radio1 followed by full-topology deployment returned success. The radio's container ID, telemetry boot ID and status-service boot ID changed. The other seven container IDs/start times remained unchanged.
- Recreated eth1 regained UP, MTU 9000 and 10.79.0.10/24. Status confirmed streaming and configured center frequency, sample rate and bandwidth.
- Fresh target detections appeared approximately 51 seconds after full-topology deploy completed, within the independently specified 90-second bound. A further 30-second window contained fresh valid detections for all four streams; detector datagrams advanced from 139415 to 196468 without a detector process restart.
- Controller reported one boot change, five configurations and five submitted/admitted starts: four initial plus one replacement. Exactly one radio_restarted event targeted radio1 with a later epoch (1790380718.045570821 versus original 1790380631.537090969). Streaming and detections independently corroborated start admission.
- Final controller totals: 21 connection attempts, 3 connections, 11 reconnects, 9 protocol failures, 4 status failures. These are aggregate counts, not a claim of error-free recovery or an exact attribution of each failure. No controller_error records were emitted in the collected processor logs.
- Processor packet_gaps increased from 0 to 10; detector recorded 1 unavailable and 5 gapped observations, with zero malformed packets and zero detector sequence_gaps. These distinct counters do not establish a packet-loss count. Processor send_failures remained zero.
- Generated input hashes were identical before/after. Scoped lab destruction exited 0; no containers remained. VM state is Stopped.

## Documented and limitations

Source inspection is recorded separately in SOURCE_INSPECTION.md. `stale_starts_replayed` is zero but has no increment site, so it is not a standalone replay detector. Exact start counts and later-epoch evidence support the bounded case; command-by-command packet tracing was not run.

This is disruptive recovery of one synthetic radio, not lossless failover, a latency SLA, an all-role recovery matrix or AMD64 qualification. Timing/retry causes were not instrumented sufficiently to explain each reconnect. Status-service and telemetry boot IDs are separate identities and are not expected to equal each other.

## Verification and unrun checks

PASS: source-integrity verification, Python harness compilation, Linux image build, actual eight-node baseline/replacement/30-second sustained runtime checks, controller counts, source hash preservation and cleanup. No new application source changes were necessary. Existing unit/telemetry suites were not rerun in this slice; their prior evidence remains historical. Processor/recorder/switch replacement, repeated radio replacement, all-radio failure and wire-level stale-command tracing remain unrun.

Next bounded step: qualify processor replacement and reattachment to radios that are already streaming, including how existing sessions and start epochs are handled. Do not infer this from radio replacement.

See README.md for reproduction, PLAN.md for pre-execution expectations, guest/trial for snapshots and logs, SUMMARY.json for actual checks, and vm-final-state.json for cleanup confirmation. Full JSONL logs remain local with hashes in raw-log-hashes.json.
