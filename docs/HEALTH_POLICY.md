# Local activity health policy

New records carry `health_policy: "local-activity/2"`. This is a change from lifetime-error degradation; consumers must inspect the policy version rather than assume old semantics. The `vrt.telemetry/1` envelope and cumulative counters remain intact. Unknown policies must not be interpreted as proof of health.

Heartbeat and shutdown `data.health` includes scope (`local_activity`), reason, error_total (the role's supplied cumulative error/discard aggregate), new_errors, counter_reset, sample_window_ms, and activity_window_ms. The first sample window is null; its baseline errors are zero for the current process. A decreased counter has a null new_errors value, never unsigned underflow or fabricated zero.

| State | Evidence |
| --- | --- |
| degraded | Cumulative errors increased since the previous reporting observation |
| healthy | Recent successful local activity and no new errors; after an error/reset, activity must occur after that observation and at least one reporting interval must elapse |
| idle | No successful local activity, or none within twice the configured reporting interval; this is not failure or confirmed service health |
| unknown | Counter reset, recovery not yet supported by fresh activity, or graceful shutdown |
| failed | Explicit caught fatal error; latched for that Reporter, including subsequent shutdown |

On counter reset, a new baseline is established but recovery still needs fresh activity and a quiet interval. The reset remains visible in the event; the Reporter never changes application counters. With new errors and no activity, degraded takes precedence for that observation. With no new errors and no recent activity, idle takes precedence over waiting for recovery evidence. Default reporting is 5 seconds; activity freshness is therefore 10 seconds. Disabling telemetry does not create health events.

`ready` still means local initialization, never coordinated radio readiness, connectivity or delivery. Shutdown is never ready. Detection events remain unknown and do not themselves assert application health. A healthy local processor can coexist with missing streams, unavailable downstream consumers or failed controller sessions: inspect those independent facts. The policy does not certify measurement timestamps, all-stream completeness or end-to-end success.

Historical waiting_context discards remain in discarded/malformed counters and reason totals. They cause degradation when newly observed but no longer permanently prevent healthy local activity after recovery. No samples are fabricated, no counters reset, and no packet acceptance or controller behavior changes in this slice.

Verification covers explicit monotonic-time state transitions, fatal latching, counter resets, a real UDP error/valid-traffic/silence scenario, output bounds/concurrency, and a fresh VM processor-replacement trial. See [health qualification](../artifacts/health-recovery/RESULTS.md) for actual results and limitations.
