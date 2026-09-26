# Patched Linux ARM64 qualification

The session manifest identifies the fresh VM and exact source archive. Source changes made during qualification are preserved in qualification-changes.patch. This directory preserves failed attempts; no historical result is replaced.

## Completed evidence

**Observed:** Linux production and Debug test images build against pinned dependencies and the verified VRT patch. All 15 Debug CTest targets pass (`guest-build-final.log`); macOS also passes 15/15 (`macos-ctest.log`). The six isolated telemetry container cases pass. Recorder readiness cases pass (`link-readiness.json`).

**Observed failures and corrections:**
- GCC treated upstream indentation warnings as errors for several standalone tests. Their third-party include directories now use SYSTEM consistently with production targets. Project code retains warnings-as-errors.
- The test container initially omitted generated configuration. The explicit test invocation now mounts the exact generated fixtures read-only; application expectations are unchanged.
- The exclusive-control socket test could outlive its unrelated 120 ms stale-owner timer on Linux. It now uses a separate 2-second ownership window and waits boundedly for a nonblocking close, asserting both rejection count and retained original ownership. The dedicated stale-expiry case retains its 120 ms limit. EAGAIN and the unsuccessful intermediate assertion are preserved in diagnostic logs.
- Deployment initially failed because the recorder opened a newly created but administratively down interface and exited on a packet-socket error. A down/up isolated probe reproduced that behavior. The recorder now waits for IFF_UP within its existing 30-second readiness deadline. A delayed-up test succeeds; a never-up test fails boundedly. Capture after an established interface fails remains a separate behavior.

## Switched workflow and measured limits

**Observed:** the patched production image is ARM64 `sha256:9f5b02b86e7ee3b0bea7ba72c07b46d9a11cb19e77f2b9947f59ac957a9c49b7`. The full eight-node lab deployed and completed a 340-second sustained observation window after startup. The original trial-status elapsed number includes subsequent faults; use the explicit 340-second progress log for sustained duration. All four radios remained streaming beyond 256 control requests, with no restart/OOM in that window. The four configured tones were detected within half a 488.28125 Hz FFT bin. Processor malformed packets, packet gaps and send failures were zero; detector malformed packets and sequence gaps were zero. These are observed counters, not proof of lossless networking. See ANALYSIS.json and guest/sustained.json.

**Observed:** final controller metrics before producer shutdown report four configurations, four starts submitted/admitted, zero protocol failures, zero reconnects, and zero stale-start replay. Four status failures occurred during startup; this is not a zero-startup-error claim. Exact first-sample epoch equality is verified by the Linux controller integration test; the switched trial did not independently capture every radio's first packet.

**Observed:** JSON event sizes, timestamps and increasing per-boot sequences pass the retained-log analysis. Isolated Linux process tests cover idle, malformed input, shutdown and new boot identity/counter reset. Measured state remains distinct from downstream delivery, which is explicitly unknown.

**Observed limitation:** stopping/starting the detector directly with Docker produced a new boot ID but no resumed datagrams in the 15-second observation. A new process is not proof of restored Containerlab networking. The later interface inspection was too late (the container was already destroyed), so lost link attachment is **Inferred**, not established by that inspection. Complete lifecycle-aware container restart recovery is **not qualified**. Five processor send failures appeared during the deliberate downstream outage, after the clean sustained window.

**Observed capture limit:** the finite 60-second capture wrote 67,104,980 bytes / 11,916 frames with zero PCAP I/O errors. It reached the 64 MiB configured limit and measured 9,110 packet-socket kernel drops. This qualifies bounded recording and finalization, not capture completeness. Durable fsync commitment remains unmeasured. See capture-metrics.json.

**Observed overhead:** five interleaved Linux Debug loopback samples per mode yielded mean CPU 8.87 ms disabled, 11.94 ms at the default 5-second interval, and 12.10 ms at 100 ms. Default overhead was about 3.07 ms CPU (+34.6% relative to this very small baseline); mean wall time was 0.516 s disabled versus 0.513 s default. This short timing-driven workload includes initial/final events and cannot establish sustained production throughput cost. See overhead.json for all samples and conditions.

## Supplemental fault trial and cleanup

**Observed:** the supplemental trial paused the detector for 10 seconds, then unpaused it with Containerlab links preserved. Datagrams resumed under the same boot identity; after producer interruption the detector reported idle. All application shutdown exit codes were zero. This is pause/resume recovery, not a substitute for the unsuccessful direct Docker restart recovery. See fault-analysis.json and guest/faults/.

**Observed cleanup:** both lab destroy audits passed. The one exited intermediate container retained by the failed Docker test build was explicitly identified and removed; the dedicated daemon then had no containers. VM `clab-vrt-linux-20260925-220209` is **Stopped** (`vm-final-state.json`). No pre-existing VM was accessed. Its stopped disk retains images and evidence; no disk deletion was requested.

## Reproduction and remaining scope

Use README.md in this directory and the preserved guest scripts. Failed attempts and corrections remain separate. Full raw synthetic JSON logs remain locally available under guest/ and are hashed in raw-log-hashes.json; raw PCAP is kept only in the guest/ignored runtime storage. No packet payload is committed.

AMD64, physical radio timing, lossless capture, slow logging sinks and complete lifecycle-aware container restart recovery remain unqualified. Ordinary tests do not create VMs. No pre-existing VM or sibling workspace was changed.

## Assessment and next step

The patched ARM64 images are qualified for the demonstrated coordinated, sustained test workflow, bounded telemetry and recorder behavior. Qualification is partial for recovery and capture completeness. The next bounded task should qualify a Containerlab-authoritative detector/node replacement or redeployment workflow that restores its links and traffic, preserving exact instance identity and telemetry reset semantics. Do not advertise bare Docker restart as supported network recovery. Lossless capture requires a separately specified rate/buffering/storage acceptance target.
