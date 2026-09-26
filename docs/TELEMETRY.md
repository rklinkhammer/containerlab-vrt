# Application telemetry v1

All four application roles emit newline-delimited JSON with `schema: "vrt.telemetry/1"`. Legacy status, controller, detection and metrics records remain for compatibility; select the schema field when consuming new telemetry. Plain diagnostic stderr is separate. Neither heartbeat nor Docker running state establishes end-to-end health.

The machine-readable envelope is [telemetry.schema.json](telemetry.schema.json); role data semantics below are normative.

## Envelope and state

Fields: `event` (heartbeat, detection, shutdown, failure), `timestamp` (UTC RFC3339 milliseconds), `role`, `instance` (maximum128 bytes before JSON escaping), `boot_id` (random32-hex process identity), `sequence` (process-local monotonic emitted-attempt number; gaps possible for suppressed events), `uptime_ms` (monotonic since reporter module process initialization), `ready`, `state`, `last_successful_activity` (UTC or null), `suppressed_events`, and `data`.

Counters accumulate per process and reset on restart; boot_id distinguishes resets. Sequence is shared within a process. `ready` means local initialization, not coordinated radio start, remote connectivity or delivery. `healthy` means recent successfully handled local input/submission/capture; `idle` means no such activity within twice the configured interval. The versioned `health_policy: "local-activity/2"` marks new cumulative-error increments `degraded`. Recovery requires a quiet reporting interval and successful local activity after the error observation; cumulative counters remain unchanged. Recent activity predating the error does not prove recovery (`unknown`); absent/stale activity is `idle`. Counter decreases are reported as `unknown` with a null delta. A caught fatal application error is latched `failed`. Shutdown is `unknown` and not ready unless a fatal failure is latched. See [health policy](HEALTH_POLICY.md) for exact evidence and migration rules. These are reporting semantics, not comprehensive health checks.

`last_successful_activity` means valid local datagram processing for processor/detector, successful transport submission for radio (control or data), and receipt of a frame for recorder. Recorder receipt does not imply successful file writing. Failures during very early argument/config validation may produce only a generic stderr diagnostic if telemetry itself cannot initialize.

## Role measurements and units

| Role | Data |
| --- | --- |
| Radio | Control receive packets/bytes; transmit bytes accepted by socket across control/data; completed/failed submissions; write retries; sample ordinal; clipped samples; streaming flag |
| Processor | Receive datagrams/bytes; malformed count; context updates; packet-count discontinuity units (`packet_gaps`); generated spectra; successful UDP sends and bytes; send failures; unavailable processing count; total measured core processing nanoseconds and call count |
| Detector | Receive datagrams/bytes; malformed count; spectrum sequence-gap units; detection count; gapped/zero/unavailable observations; total measured core processing nanoseconds and call count |
| Recorder | Received Ethernet frames/wire bytes; truncations; receive errors; observed Linux kernel drops; PCAP frames/bytes accepted by the writer, limit reached and I/O errors |

The processor/detector receive bytes count bytes supplied to the core, not uncaptured network wire bytes. Processing time brackets core consume only; it excludes network wait, logging and downstream latency. Rates can be derived from counter and monotonic-uptime deltas within one boot; never join counters across boot IDs. Packet-count discontinuities can result from reordering/duplicates/wraps and do not prove packet loss. Unknown queue depth, proven network loss and downstream delivery are null. Detector transmit counters are null because it does not transmit application packets.

PCAP bytes include headers and buffered writes accepted by `ofstream`. Flush is not fsync; `durably_committed_bytes` is null. Kernel drops are measured at the packet socket, not a claim about all upstream losses. Capture limit exhaustion is explicit even without an I/O error.

Detection records include emitter identity in the envelope and stream_id, source observation seconds/picoseconds, detected_frequency_hz (or null), and validity in data. No packet/sample payload is logged. Detection count includes all core detections; event output is independently limited to10 records per1-second window. Legacy detection output retains its prior per-stream observation-second throttle.

## Configuration and bounds

`config/lab.json` contains `telemetry.interval_ms`; generation writes `VRT_TELEMETRY_INTERVAL_MS` into application-node environment. Default5000ms; application polling can add up to100ms (processor/detector) or200ms (recorder) scheduling jitter; accepted100..60000ms;0 disables new telemetry but leaves legacy logging/counters. Direct processes can set the environment variable. `VRT_INSTANCE_ID` optionally supplies a reviewed label; otherwise radios use their configured ID and other applications use hostname.

Records are at most4096 bytes including newline. Oversized records and excess detection events are suppressed, counted and reported in the next successful telemetry record. JSON safely escapes untrusted instance text. There is no unbounded telemetry queue: each event is serialized and synchronously flushed under a shared mutex. This preserves complete lines across telemetry/controller writers but a slow stdout consumer can delay processing. Logger/daemon delivery and drop counts are unknown; application suppression counts do not include daemon losses. Do not put secrets in instance labels.

For dedicated test Docker hosts, use bounded logging such as `json-file` with `max-size=8m`, `max-file=3`; optionally non-blocking delivery with `max-buffer-size=1m` if bounded loss is preferable to backpressure. These daemon settings affect newly created containers and require a reviewed daemon restart; do not overwrite unrelated host configuration. Qualification uses per-container rotation flags. Docker daemon rotation does not establish durable telemetry retention. Do not claim drop-free output in non-blocking mode.

## Verification and use

```sh
python3 scripts/generate_config.py
python3 scripts/generate_config.py --check
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
python3 scripts/test_telemetry_process.py
python3 scripts/benchmark_telemetry.py
```

See the existing macOS guide for local prerequisites and isolated Linux image builds. `build/dev` must already be configured with pinned nlohmann/json and SoapySDR. Ordinary checks above never create a VM. Linux recorder checks require the explicit fresh-VM procedure in artifacts/telemetry/README.md. Qualification results and baseline failures are in artifacts/telemetry/RESULTS.md.

After deploying through the existing reviewed Containerlab workflow, use `sudo docker logs --follow <exact-container-name>`. Ctrl-C stops following, not the application. The generic containerlab-workspace node-log view can already display these JSON lines. A structured dashboard/parser is separate future work; no GUI or topology validation should assume four radios, VITA roles or these node names.
