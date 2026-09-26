# Processor recovery fix — qualified bounded ARM64 case

## Observed

Fresh VM `clab-vrt-processor-fix-20260926-002402`; Containerlab 0.79.0; application image `sha256:66fabae4a1d5f1a68b858fed486fd3a55c3c566ab9223700c14d5e60a5a7dc1c`. The exact source archive, commit and two-patch manifest are in session.json.

- 16/16 macOS tests pass (ctest-macos-final.log) and 16/16 Linux Debug tests pass (guest/ctest.log). One intermediate test-fixture construction failure and the earlier failed fix attempts are preserved, not counted as passes.
- The focused pre-fix regression confirmed new IDs 1–3 against surviving native high-water marks 188–189. Merely advancing IDs then exposed native rejection of configuration while streaming. Both findings were addressed without disabling native checks.
- Actual Containerlab processor replacement passed the original independent identity/controller/traffic expectations. The processor ID changed; the other seven container IDs/start times and all radio boot IDs stayed unchanged. Stream IDs and all generated source hashes were preserved.
- Replacement controller: 4 connections, 4 configurations, 4 starts submitted and admitted, coordinated=true, 0 boot changes, 0 protocol failures, 0 status failures. Four native quiescence events preceded configuration. New coordinated epoch 1790382586.07405544 was later than 1790382547.989119382.
- Fresh valid detections for all four tones returned in approximately 10 seconds after deployment, within the original 90-second bound. The following 30-second window advanced detector datagrams 66454 → 125051, with valid tone measurements within half an FFT bin (244.140625 Hz).
- Transition effects remain visible: detector sequence_gaps=4, unavailable=4, malformed=0. Processor malformed rose to 130 early and stayed there through the observed remainder; processor packet_gaps/send_failures remained 0. The aggregate malformed counter has several contributing branches. The trial does not establish which branch produced those 130 counts or prove packet loss.
- Native processor interface check passed: UP, MTU 9000, configured 10.79.0.14/24. Scoped lab cleanup exited 0; no containers remained. VM state is Stopped.

## Implemented and documented

See ../../docs/COMMAND_RESUMPTION.md. The local native extension exposes the admitted watermark and an advance-only controller floor. A bounded, serialized status capability ties that value to boot/SID/association/control generation. The controller fails closed on invalid capability, reconciles native status, then stops already-streaming radios with execution evidence before configuration and a new coordinated start. Ordinary same-controller reconnect does not submit extra starts.

Tests include missing capability, SID mismatch, stale control generation, changing boot ID, exhausted/overflow watermark, invalid native handles, non-rewinding floors and retained state after exhaustion rejection. Clean-base application of both pinned patches reproduces the manifest hash (clean-patch-apply.log).

## Limits and next step

This qualifies one synthetic processor-replacement workflow on ARM64, not lossless failover, a latency SLA, arbitrary topologies, multi-user ownership or AMD64. Upgrade radio and processor together; old radios lack the required capability. The native extension is local, not yet an upstream release feature. Packet-by-packet replay injection, stress/race-sanitizer testing, multi-controller contention and repeated fault combinations were not run. The original failure artifacts remain historical evidence.

Next bounded step: classify the 130 transition discard counts with independent packet/context expectations, distinguish missing context from genuinely malformed input, and verify steady-state diagnostics after replacement. Do not reset counters merely to make health appear clean. Recorder/switch replacement remain separate unqualified scopes.
