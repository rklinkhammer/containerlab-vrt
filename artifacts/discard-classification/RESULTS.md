# Transition discard classification — completed

## Observed runtime evidence

Fresh ARM64 VM `clab-vrt-discards-20260926-003625`, Containerlab 0.79.0, application image `sha256:03a61ed0cf0c2f4779e5949892cdae97e00600dd73f0fd848c401a64cb5778ed`. The pinned source manifest, commit and source archive hash are in session.json.

- Baseline: zero processor discards.
- After processor replacement: 141 discards, **all waiting_context**. Invalid envelope/context/data/samples, limits and timestamp-range counters were all zero. These inputs passed signal-envelope/trailer and sample-view checks but arrived before this processor had valid stream context.
- All seven reason deltas were zero throughout 36 metric/heartbeat records spanning 29.08 seconds within the 30-second sustained-traffic check. No ongoing discard growth was observed in that window.
- The legacy malformed aggregate was 141, exactly equal to discarded and the reason sum. Every audited record satisfied the partition. Maximum observed record size across the three processor logs was 884 bytes, below the 4096-byte event bound. No per-packet logging or payload disclosure was added.
- Controller recovery still passed: 4 connections, 4 configurations, 4 starts submitted/admitted, zero protocol/status failures. All four tones recovered, other seven node identities remained unchanged, and generated input hashes were preserved.
- Scoped cleanup exited 0, no containers remained, and the dedicated VM is Stopped.

## Historical inference limit

The previous run's 130 aggregate counts cannot be retroactively classified. This repeat identifies a concrete cause for its own 141 counts and supports missing context as a likely explanation for the earlier startup aggregate; it is not proof of the earlier packets' individual rejection paths. No lossless-recovery claim is made.

## Implementation and validation

The processor now emits processor.discards/1 with seven fixed counters and discarded total. Native packet validation and output semantics are retained. Invalid signal shape/sample limits are checked before classifying missing context, so malformed data is not disguised as waiting_context. The legacy malformed total and cumulative-error health policy remain compatible.

16/16 tests passed on macOS (ctest-macos-complete.log) and 16/16 on Linux (guest/ctest.log). Independent fixtures cover all seven reason categories, bad input without context, exact reason sums, unchanged valid-tone output, and fresh-process counter reset. A first fixture failed because the native encoder disallows an empty signal; that failure is retained and PLAN.md explains the oversized-sample replacement fixture. The reason audit is reproducible with `python3 artifacts/discard-classification/audit_reasons.py` and performs no runtime operations.

## Limits and next step

Only the existing synthetic ARM64 replacement case was rerun. Long-duration stress, AMD64, packet-level captures, new instrumentation-overhead measurements and arbitrary workflows were not run. Context rejection categories intentionally group several static validation failures; this is a bounded reason partition, not a packet dump.

The remaining user-visible issue is current health interpretation: a historical waiting_context count still keeps the existing cumulative-error policy degraded even after processing recovers. Next define and test explicit recovery/health transitions based on recent measured activity and counter deltas, while retaining all historical counters. Do not simply erase counts or equate a heartbeat with health.

See ../../docs/DISCARD_DIAGNOSTICS.md for counter meanings and README.md for build, qualification and cleanup commands.
