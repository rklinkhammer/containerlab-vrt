# Containerlab-managed recovery qualification

## Established findings

**Observed:** current checksum-verified Containerlab 0.79.0 reports commit `5ae50094a`, matching the inspected primary source. A fresh dedicated ARM64 VM was created for this trial; no previous VM was accessed. Source and image identities are recorded in session.json and guest/image.json.

**Observed negative control:** Docker stop/start produced a new detector process boot identity but removed `eth1`. The namespace inspection explicitly returned `Device "eth1" does not exist.` Detector traffic did not resume. This establishes the missing-link cause that the previous trial could only infer.

**Observed reconciliation failure:** the native dry-run correctly reported the missing detector–switch link. Native deploy/reconciliation returned exit 0 and recreated it, but `eth1` had MTU 9500, only an IPv6 link-local address, and none of the configured IPv4 address `10.79.0.15/24`. It did not meet the required MTU 9000 or resume detector traffic within 90 seconds. Native command success and interface existence were insufficient acceptance criteria.

**Documented mechanism:** at the pinned revision, core/apply.go calls postDeployApplyNodes for deployNodeNames. For a link-only repair of existing nodes, that set does not include the detector; the generated node exec commands that establish address and MTU are not replayed. No custom link creation, schema reinterpretation or upstream patch was introduced.

**Observed fallback success:** native full `redeploy --cleanup --graceful` recreated all eight nodes, restored the detector's configured interface/address/MTU, and passed 30 seconds of advancing traffic with all four configured tones within half an FFT bin. This is a lab-wide outage and replacement, not isolated recovery. Cleanup passed with no remaining containers.

## Detector replacement and assessment

**Observed:** independent native detector-only destruction (`--node-filter detector --keep-mgmt-net --graceful`, no cleanup) followed by full-topology native deploy passed. The new node's native post-deploy commands restored IPv4 10.79.0.15/24 and MTU 9000. All four tones returned within half an FFT bin, and the subsequent 30-second advancing-traffic check passed. The final detector counter was 67,472 datagrams with zero observed sequence gaps. This does not establish lossless replacement during the outage.

**Observed identity isolation:** only the detector container ID changed; IDs and StartedAt values of the seven non-target nodes remained unchanged. The detector had a new boot ID, initial sequence 1, uptime 0 ms and zero datagrams, establishing a new counter history rather than continuity with the old process. See guest/replacement/ and SUMMARY.json.

**Observed provenance:** generated topology, all companion JSON files, switch configuration and Containerlab executable hashes matched before and after the trials. The application image built in this fresh guest is `sha256:6f193efb674c3564067d30dabf4792d678ab00a4f416a8df130a02b1584e444c`. No application, generator or dependency source was changed in this qualification; source-changes.patch preserves inherited changes. No custom topology semantics or link repair was introduced.

**Observed cleanup:** both scoped destroy audits passed and the fresh daemon had no containers remaining. VM `clab-vrt-recovery-20260925-222243` is **Stopped**. Its disk is retained. No pre-existing VM or sibling workspace was changed.

## Reproducibility and boundaries

See README.md for trial reproduction and ../../docs/RECOVERY.md for the qualified operator workflow. The initial package-install attempt collided with still-running VM provisioning; build-provisioning-lock.log is preserved. Retrying after Lima's complete READY result succeeded. This was an external prerequisite failure, not an application failure.

The 15/15 Linux/macOS test results from the preceding qualification remain historical evidence for unchanged application source; this task rebuilt the production image and ran recovery trials, not the entire unit suite again. Raw synthetic detector logs remain locally available under guest/ and are indexed by SHA-256 in raw-log-hashes.json. Reviewed JSON snapshots retain identities, interface configuration, tones and measured counters.

**Qualified:** detector replacement through native filtered destruction plus full-topology deployment; disruptive full native redeploy fallback.

**Not qualified:** link-only reconciliation for this exec-configured node, radio/processor/recorder/switch replacement, arbitrary node kinds, AMD64, lossless switchover and persistent-state recovery. The next bounded qualification should exercise radio replacement and controller boot-change handling before exposing broader recovery actions in a GUI. No GUI code or sibling workspace was changed.
