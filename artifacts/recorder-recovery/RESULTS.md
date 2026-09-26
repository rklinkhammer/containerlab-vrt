# Recorder replacement and capture preservation — PASS within scope

**Observed:** Fresh Linux ARM64 trial on Containerlab 0.79.0; VM `clab-vrt-recorder-20260926-011019` is **Stopped**. Application image `sha256:f4bf75514187f4ef445c2b60d7ab64a55b4c77965fbc7431c68f48bc61e9fdb6`. Source archive and the capture-script qualification override are pinned in session.json; native dependencies are unchanged.

## Verification

- macOS CTest: **16/16 PASS**, ctest-macos.log. Linux final image CTest: **16/16 PASS**, guest/ctest.log.
- Offline lifecycle tests: **3/3 PASS** on macOS and Linux: separate sessions, removal, caller interruption. These use mocked Docker calls, not actual runtime claims.
- Native recorder filtered destruction + full-topology deployment: **PASS**. Recorder container/boot changed; the other seven container IDs and start times remained unchanged. eth1 UP/MTU restored, mirrored frame counters advanced within 90 seconds and over a subsequent 30-second interval. All four configured detector tones remained within independent half-bin expectations; detector counters advanced during final sustained traffic.
- Original generated inputs and hashes unchanged. No topology/third-party source changes.
- Existing PCAP destinations and symlinks are refused without truncating originals; invalid byte limits do not create files (C++ tests).
- Before/after PCAPs: valid Ethernet PCAP 2.4 headers, every record boundary/length checked, no partial final record, record counts/bytes equal final metrics, zero PCAP I/O errors, within 64 MiB.
- Capturing during native removal returned **137**, retained a **36,909,056-byte partial PCAP**, could not copy metrics, and correctly recorded `incomplete`. This expected unsuccessful attempt is preserved in guest/trial/interrupted-manifest.json and interrupted-capture.log; it was not relabeled completed.
- RETENTION_AUDIT.json independently verifies all copied-file lengths/SHA-256 values in all three sessions after lab cleanup and transfer to the Mac. Complete first capture unchanged after recorder replacement. Raw captures remain ignored in artifacts/runtime/recorder-recovery/recorder-captures.tar.gz and on the stopped guest disk.

## Capture limitations — not lossless recording

| Capture | Bytes | Records | Kernel drops | Size cap reached |
| --- | ---: | ---: | ---: | --- |
| Before replacement | 67,106,704 | 11,976 | 11,037 | Yes |
| After replacement | 67,107,198 | 11,961 | 10,917 | Yes |

Each command ran 60 seconds but exhausted its PCAP size allowance after about 3.9 seconds of recorded frames. The interval between the first complete capture's last packet timestamp and the second's first was **96.381 seconds**. That interval includes intentionally uncaptured time and is neither a precise replacement outage duration nor a packet-loss count. The partial interrupted file does not fill that interval with qualified complete evidence.

**Observed:** Completed-file structure/preservation passed; capture completeness is **not satisfied**, due to drops and byte caps. The main recorder is a passive metrics process, not a persistent recording service. `fflush` is measured only as successful finalization, not fsync/durable commitment.

**Unresolved / not run:** disk-full injection, power-loss/crash consistency, continuous rotation/retention, abrupt daemon/VM failure, repeated restart races, long stress, AMD64, switch replacement. No overhead benchmark was rerun. Historical failures and prior qualification scopes remain unchanged.

## Cleanup and next step

**Observed teardown limitation:** The final ordinary `scripts/destroy.sh` run logged a recorder `APPLICATION_ERROR` as Containerlab removed its link/container. The preceding traffic checks passed. The sanitized application event does not identify the exact internal exception, so link-removal/socket failure is an inference, not a confirmed cause. Graceful recorder shutdown during full-lab teardown is not qualified by this result; the log is retained in guest/trial/cleanup.log.

Exact task lab cleanup exit **0**, no remaining containers; the management-network cleanup audit passed. New VM stopped after evidence transfer. No pre-existing VM was accessed or modified. Reproduction and recovery commands: README.md and ../../docs/RECOVERY.md.

Smallest next gated qualification: **SR Linux switch replacement**, explicitly testing link/interface restoration and recovery of existing endpoints/controller/data/mirroring. Do not infer it from successful leaf-node replacement. Continuous loss-bounded recording is a separate feature if required.
