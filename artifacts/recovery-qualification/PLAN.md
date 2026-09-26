# Containerlab-managed detector recovery

Question: can the pinned native lifecycle restore the detector's link and actual four-stream processing, preserving unaffected node identities?

Scope: current reviewed four-radio fixture, unchanged original topology/config bytes, Containerlab 0.79.0, verified patched application source, ARM64 only. New dedicated VM in session.json; no existing VM access. No GUI changes, custom link reconstruction or alternate deployment engine.

Independent acceptance expectations (before runtime):
1. Initial full lab has all eight running nodes; detector eth1 is up with the configured address and MTU. Increasing detector datagrams and all four configured tones within half an FFT bin establish application traffic.
2. Reproduce Docker stop/start as a negative control and record actual interface state, container ID, process boot ID and traffic. Do not infer missing links from absence of traffic alone.
3. Inspect installed CLI help/version against pinned primary-source interfaces. Where supported, inspect native dry-run then reconcile the complete original topology (no filtered deploy against a live lab). Recovery requires restored eth1/address/MTU, advancing detector datagrams and valid detections for every configured stream. Record all changes in container IDs and StartedAt; non-target nodes should be unchanged for an isolated-recovery claim.
4. If supported, destroy only detector using the native node filter without cleanup, keep management networking, then deploy the complete original topology. Expect a new detector container/process identity and reset sequence/counters, with all other container identities unchanged. Repeat traffic verification for at least 30 seconds after each successful recovery.
5. If targeted recovery is unsupported or fails, preserve it and qualify full native redeploy as an explicitly disruptive fallback. Never report full-lab replacement as isolated node recovery.
6. Failures are classified as native lifecycle/interface, application transport/control, validation harness, or external prerequisite. A successful command or running process alone does not satisfy recovery.
7. Scoped lab destruction, no residual task containers/network, and stopped new VM are required even after failure.

Source inspection: read-only investigation checkout at 5ae50094a3afd70e4e1674fe5385e64d8979da26, tag v0.79.0. core/deploy.go exposes full-topology reconciliation and rejects node-filter reconciliation; cmd/destroy.go rejects cleanup with node-filter; cmd/redeploy.go invokes destroy followed by deploy. These are Documented source findings, pending installed-binary interface/runtime verification.
