# Processor replacement qualification — FAIL

## Observed

Two attempts in fresh dedicated ARM64 VM `clab-vrt-processor-recovery-20260926-000236` used Containerlab 0.79.0 and application image `sha256:53176485b13364df4e18cd4e8e6f275b13978679d0d8cc566b10bb2898d82125`. The second attempt followed complete first-lab cleanup in the same still-running task guest; no stopped or pre-existing VM was reused.

- Native processor destruction/replacement succeeded. Processor container and telemetry boot identities changed; other seven node identities/start times, radio status boot identities and detector boot identity remained unchanged (identity-audit.json).
- All four radios reported streaming while the processor was absent. This is status evidence at those samples, not proof of uninterrupted delivery.
- The new processor received existing streams and generated fresh correct detections for all four tones within half an FFT bin. Both attempts passed 30-second increasing-traffic checks. Repeat detector datagrams increased 62197 → 120798, with four sequence gaps and zero malformed packets. Data-plane recovery alone is insufficient.
- Controller recovery failed. Repeat totals: 42 connection attempts, 53 protocol failures, zero successful connections/reconnects, zero configurations, zero submitted/admitted starts, coordinated=false. Status-service readiness over framed TCP is not VRT transaction success.
- Exact repeat failure: `CONTROLLER_RECOVERY_TIMEOUT: no coordinated controller with four active radio sessions within 90 seconds after traffic recovery`. This deadline followed the 30-second recovered-traffic window, so the controller had more time than the original traffic deadline required.
- Native processor logs repeatedly report `controller_error`, `reason=vrt_status_failed`, with observation_kind 6 (timeout) or 7 (late_response); route_failures reported zero in collected records. A TCP connection marked active did not establish successful VRT status reconciliation.
- Attempt 1 stopped on an inactive-session assertion with an empty reason. Its original script, logs and SUMMARY are preserved. ATTEMPT_2.md explains the diagnostic improvement and separate controller deadline; acceptance outcomes were not weakened.
- Both cleanups exited 0 with no remaining containers. Input hashes remained unchanged. The VM is stopped.

## Inferred; unresolved

A likely cause is controller transaction identity reuse: a fresh relationship starts next_mid at 1, while the surviving SDR manager enforces monotonically increasing IDs with highwater_id. Application transport reconnects ingress but does not explicitly negotiate a new control association. Relevant pinned source is recorded in SOURCE_INSPECTION.md. This is a hypothesis, not a confirmed packet-level diagnosis; do not disable replay/ordering checks or randomly raise IDs as a workaround.

## Verification scope

PASS: pinned source verification, Python harness compilation, Linux image build, baseline and recovered traffic, post-run identity audit, unchanged inputs, cleanup. FAIL: controller reattachment/re-coordination in both attempts. The processor-interface assertion was not reached after controller failure; native deployment logs show commands, but no separate interface snapshot establishes that gate. No new application changes were made. Unit suites were not rerun; packet-level transaction tracing, targeted replacement regression, a fix and its runtime requalification remain unrun.

## Next bounded implementation

Build a deterministic regression with a surviving radio that has processed many commands, then destroy/recreate only its controller. Trace request IDs, association generations and acknowledgements. Confirm the failure cause and use the supported native association/session lifecycle to recover without weakening stale-command protections. Verify both fresh-controller recovery and same-controller reconnect, plus rejection of old commands. Only then repeat processor replacement in a new dedicated VM. Until then, use the previously qualified full-lab native redeploy as the disruptive fallback; isolated processor replacement is not qualified.

Reproduction and cleanup commands are in README.md; both attempts and the initial expectations are retained alongside SUMMARY.json.
