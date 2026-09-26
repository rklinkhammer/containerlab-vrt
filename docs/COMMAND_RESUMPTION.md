# Controller command-ID resumption

The pinned SDR runtime preserves monotonically increasing command IDs across TCP reconnects. A replacement processor starts with fresh local state and must not reuse IDs from the surviving radio association. Stream IDs remain unchanged.

## Native extension

`patches/vrt-command-resumption.patch` adds serialized-domain APIs:

- `Controllee::admitted_message_id()` exposes the native manager watermark.
- `Controllee::association_generation()` identifies that native association.
- `Controller::resume_commands_after(watermark)` advances the next ID to at least watermark + 1. It never decreases an existing counter or clears retention. UINT32_MAX is refused; invalid registry handles fail. Exhaustion requires a separate lifecycle transition, not wraparound.

The extension is a local reviewed patch against the pinned revision, not an upstream feature claim. `container/vrt-source.json` pins both patches and the resulting header tree. Apply with `python3 scripts/verify_vrt_source.py --apply` on clean pinned headers. An older already-patched checkout must be updated deliberately; the verifier refuses unknown/intermediate trees.

## Versioned status capability

Existing status protocol version 1 gains a required capability for the new processor:

```json
{"command_resume":{"version":1,"sid":1,"association_generation":1,"last_admitted_id":189}}
```

This object belongs to the same snapshot as top-level radio_id, boot_id and connection.active/generation. SID 1 and watermark 189 are illustrative, not fixed production values.

Status workers request a fresh snapshot. The radio runtime thread constructs it after progress, including the native watermark and control slot, and publishes it under a mutex. Worker waits are bounded at 500 ms; they never read native runtime state concurrently. Existing status frame/connection limits remain in effect.

The controller reads status, acquires a fresh exclusive control connection, then reads again. It requires matching boot/native association, configured radio/SID, active control, and a new control-session generation. Missing/malformed capabilities and exhausted IDs fail closed. Only after advancing the ID floor and successful native VRT status reconciliation is the session marked reconciled. This is for the existing trusted test environment, not an authenticated multi-user handover protocol.

## Reconfiguration is disruptive

Native SDR configuration and start are illegal while streaming. For a newly unconfigured controller session, a streaming radio is stopped through the native controller API, with execution evidence required before configuration. Four configurations and a later coordinated start follow. Ordinary reconnect of an already-configured controller retains its session and does not issue another start.

This preserves radio processes and stream IDs, not uninterrupted samples or the old coordinated epoch. Continued data traffic alone does not establish controller recovery.

## Compatibility and verification

Upgrade radio and processor images together. A new processor rejects old radios without the capability. Local tests cover actual controller replacement, ordinary reconnect, missing/mismatched/exhausted/overflow capabilities, changed boot/session identity, non-rewinding floors and exhaustion. See artifacts/processor-recovery-fix for Linux runtime evidence and exact limits. Existing historical failure evidence is retained separately.
