# Bounded telemetry implementation results

## Implemented

Versioned vrt.telemetry/1 JSON heartbeats for radio, processor, detector and recorder; UTC/process identity/monotonic uptime/sequence/readiness/activity; per-boot counters; null unsupported metrics. Default5s interval, configurable100..60000ms or0 disabled. Detection telemetry limited10/window-second, record size4096 bytes, suppression count and synchronized complete-line writes. Existing VITA semantics, legacy status and summaries remain; third-party source unchanged. Serial ports, GUI changes, brokers and databases were not introduced.

Processor/detector report packet/byte counters, malformed/discontinuous input, measured core processing time, detection/gap state and actual send failures. Radio adds measured transport bytes and existing submission/sample counters. Recorder reports frame/socket drops and PCAP accepted bytes/errors separately from unknown durability. Detector silence no longer skips periodic heartbeat and unavailable-stream reporting. Documentation: ../../docs/TELEMETRY.md, schema ../../docs/telemetry.schema.json. Actual sanitized sample records: example-events.jsonl and linux-results.json.

## Actual verification

- macOS build PASS; CTest12/14 PASS. The same two pre-existing failures remain: sdr_vrt_runtime_retention_tests (query assertion) and sdr_processor_controller_tests (coordinated-start assertion). Baseline was10/12 before adding two passing telemetry targets. No expectation weakening or third-party repair.
- New telemetry contract tests: JSON validity/escaping, required identity, disabled mode, concurrency, byte limit, event-rate suppression and shutdown PASS.
- New native loopback pipeline test: valid detection, interrupted traffic/unavailable event, sequence discontinuity, failed UDP consumer with observed send error and unknown downstream delivery/loss PASS.
- Process tests: no-input periodic heartbeat, malformed datagrams, continued heartbeat after input stops, restart boot-ID change/counter reset and shutdown PASS. Initial wrong CLI duration flag failure retained in process-tests-wrong-cli.log; corrected harness uses existing --duration-seconds interface.
- Generator4 tests PASS; generated freshness and VRT source hash/metadata verification PASS.
- Fresh dedicated Linux VM: final pinned-input application image build PASS;6 actual container cases PASS: processor idle, radio idle/graceful termination, detector idle, recorder empty capture, recorder /dev/full write failure, recorder invalid metrics path. Standard Docker stdout/stderr output collected. Image ID in image-id.txt; source/image pins in source-pins.json.
- git diff --check PASS. No application or investigation sibling changed.

## Measured overhead

Three interleaved repeats per mode of the same finite loopback UDP integration workload, macOS Debug build. Disabled mean CPU 14.109ms; default5000ms mean 14.928ms: +0.819ms (+5.8%). At100ms mean 17.430ms. Mean wall times: disabled 0.555s, default 0.518s,100ms 0.516s. These timing-driven, small-sample results are noisy and do not show a meaningful throughput improvement or production overhead bound. All modes retain baseline legacy logging and added counters;0 disables only the new reporter. Full samples and limitations: overhead.json. Instrumentation benchmarks do not qualify saturation behavior.

## Cleanup and unrun checks

VM clab-vrt-telemetry-20260925-172653 confirmed Stopped. Zero residual containers before stop. No Containerlab lab was created; isolated task containers exercised process behavior. No pre-existing VM accessed. cleanup.json/vm-stop.log record outcome.

NOT_RUN: full eight-node switched coordinated-streaming qualification on the current VRT revision, Linux sustained-throughput overhead, full disk exhaustion, serial-console/capture GUI integration, arbitrary NOS/application logs and durable recorder fsync. /dev/full tests write failure, not all filesystem failure modes. Known baseline retention/coordinated-start failures block a broad end-to-end claim. No successful UDP send or heartbeat proves delivery or application pipeline health.

## Limitations and next step

Heartbeat scheduling follows application polling (up to100ms processor/detector and200ms recorder jitter); it is not a watchdog for stalled loops. JSON writes are bounded in size and synchronized but synchronous: slow log consumers can delay work. Docker non-blocking delivery can instead drop records; daemon losses are not included in application suppression counts. Application errors keep heartbeat state degraded for that boot; shutdown does not imply durability. Ready is local initialization only. Existing controller metrics/events remain distinguishable legacy records; full coordinated health is not inferred.

Use README.md here for commands, docs/MACOS.md for new-VM setup, and docs/TELEMETRY.md for log retention and semantics. The smallest next step is separately diagnosing the two baseline VRT failures, then rerunning the actual four-radio workflow in a new VM. Generic GUI node logs can display these records now; structured graphical interpretation remains separate and must not hardcode this example's characteristics.
