# Patched Linux qualification

Run only in the newly created VM identified by session.json. No pre-existing VM access.

Independent acceptance expectations, recorded before execution:
- Verify source pin plus patch hashes and record built image identities.
- Full Linux Debug CTest suite passes, including 300-transaction retention and deterministic early/on-time/late start tests. Preserve failed attempts.
- Deploy reviewed generated eight-node topology; all four radios stream, processor admits four starts, detector detects the four configured synthetic tones. Do not infer successful execution from admission alone.
- Sustain at least 331 seconds after startup, crossing the 256-entry controller registry at the configured query interval; no protocol/capacity errors, restarts or OOMs. Report observed data gaps separately.
- Validate bounded JSON heartbeats, counters and process-start identities; exercise interrupted traffic and restart, isolated recorder failures and telemetry overhead on identical workloads.
- Verify recorder writes and shutdown; collect metrics without treating buffered writes as durable storage or sequence gaps as proven loss.
- Destroy only task lab/resources; verify residuals and stop this VM even if qualification fails.

Ordinary tests do not create VMs. Linux images are ARM64 only; AMD64 and physical radio timing are outside this qualification. Containerlab 0.79.0, SR Linux image and build dependencies remain pinned by existing repository recipes. No capture completeness claim without evidence.
