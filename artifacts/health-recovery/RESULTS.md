# Local health recovery — implemented and qualified

## Observed

Fresh ARM64 VM `clab-vrt-health-20260926-004831`, Containerlab 0.79.0, application image `sha256:4906f9bc96ccc3e409124f579e3408a1b3a6416583a246d3911a61f9e27f8956`. Source and pin hashes are in session.json.

Actual replacement-processor timeline (UTC, same boot):

| Time | State | Cumulative errors | New errors |
| --- | --- | ---: | ---: |
| 00:54:46.369 | idle | 0 | 0 |
| 00:54:51.438 | degraded | 145 | 145 |
| 00:54:56.439 | healthy | 145 | 0 |
| 00:55:26.440 | healthy | 145 | 0 |
| 00:55:27.318 | unknown, shutdown/not ready | 145 | 0 |

All 145 discards were waiting_context. Recovery followed successful activity after the degraded observation and a quiet 5-second interval. Counters were not reset, hidden or reclassified. HEALTH_AUDIT.json records all intermediate samples and evidence fields; audit_health.py independently checks the transition, error aggregate, boot identity, fresh activity and shutdown semantics.

Controller/data recovery still passed: four connections/configurations/submitted and admitted starts, zero controller protocol/status failures, all four tones recovered, and other seven node identities unchanged. Generated input hashes were unchanged. Scoped cleanup exited zero with no containers remaining; the VM is Stopped.

## Verification

- 16/16 macOS tests passed, plus the focused final Reporter fatal-latch test after it was added.
- 16/16 Linux tests passed on the final source, including all added cases.
- Deterministic supplied-monotonic-time tests cover new errors, recovery without/with fresh activity, inactivity, counter decrease/null delta, shutdown and latched fatal failure.
- Real UDP detector test observes malformed input → degraded, valid local processing → healthy with malformed still 1, then silence → idle. Its detection-unavailable behavior remains tested separately.
- Existing rate/concurrency/disabled-output/size checks pass. All audited actual processor JSON records remain within 4096 bytes.

## Contract and scope

`health_policy: local-activity/2` explicitly versions the new interpretation. Heartbeat/shutdown data.health exposes local scope, reason, cumulative and delta errors, reset detection and observation/activity windows. The original telemetry envelope and role counters remain compatible. See ../../docs/HEALTH_POLICY.md and ../../docs/TELEMETRY.md for exact ordering and consumer migration.

Healthy means recent successfully handled local activity with no newly observed errors. It does not mean all streams are present, controller sessions are healthy, downstream delivery succeeded, measurement timestamps are fresh, or a complete workflow is ready. A heartbeat alone cannot restore healthy state. Idle is absence of recent measured activity, not proof of a fault.

No new overhead benchmark, race-sanitizer run, long-duration stress, AMD64 trial or real application fatal-crash injection was performed. Fatal/reset behavior has deterministic unit and Reporter coverage, not a new VM crash trial. No sibling GUI or investigation files were changed.

Next bounded runtime gap: recorder replacement and capture-file preservation. Preserve completed evidence files and qualify a distinct new recording instance; do not imply continuous or lossless capture from process recovery. Switch replacement remains separate and unqualified.
