# Implement a standalone four-radio SDR lab with Containerlab and Nokia SR Linux

## Authority and packaging

Containerlab is the topology and lifecycle authority. `config/lab.json` is the sole application-parameter source, and `scripts/generate_config.py` produces topology, SR Linux startup commands, role configurations, and a hash manifest. Deployment first rejects stale outputs.

`Dockerfile` independently builds the project-owned sources and migrated VRT headers. It does not copy, mount, link, or execute graphx-docker. The runtime image contains four commands in `/opt/containerlab-vrt/bin`: `radio`, `processor`, `detector`, and `recorder`. It contains no privileged daemon socket and receives no such mount from the topology. The recorder alone receives `NET_RAW`; the application nodes are not privileged and have Docker's default no-restart behavior.

Dependency and source pins are recorded in `container/dependencies.env`, `container/DEPENDENCIES.md`, `THIRD_PARTY_NOTICES.md`, and `config/lab.json`.

## Protocol boundaries

VRT control is plain TCP on ports 18401-18404. It retains VRT packet-size framing without an added prefix. IQ and spectrum data are UDP on ports 18501-18504 and 18600. These nine application flows use `eth1` through SR Linux.

Radio lifetime status is a separate plain-TCP management flow on ports 18701-18704. Each connection accepts one request and returns one response, each framed by a four-byte unsigned big-endian JSON length. Request and response payloads are capped at 1024 and 4096 bytes, transaction time at two seconds, and concurrent connections at four. `scripts/status_probe.py` applies the same response and timeout bounds. Status does not own or preserve the VRT control slot.

The recorder passively receives mirrored Ethernet frames on `eth1`, uses a fixed 4 MiB socket receive request, publishes schema-1 JSON counters once per second, and reports Linux packet-socket drops. `scripts/capture.sh` starts a second recorder process for all mirrored frames on that exact interface; it accepts no caller-supplied filter and is bounded to 60 seconds and 64 MiB.

All transports are unauthenticated and unencrypted for the isolated trusted lab. Logical controller ownership is not peer authentication, and this implementation makes no malicious-peer protection claim.

## Lifecycle invariants

- Build checks generated-file freshness and verifies all four image commands with networking disabled.
- Deploy accepts no arguments, rejects any pre-existing exact lab container or management-network name, and removes only topology-owned resources after partial failure.
- Inspect emits Containerlab state, Docker state, bounded radio status responses, bounded log tails, and attempts SR Linux operational interface state.
- Capture addresses only `clab-four-radio-sdr-recorder`, `eth1`, and two exact temporary filenames.
- Destroy addresses only the generated topology, its eight exact container names, and its exact management network, then performs a residual audit.
- No lifecycle script invokes global prune, wildcard deletion, a daemon socket mount, or Lima.

## Runtime qualification environment

Integrated checks ran on 2026-09-25 in the newly created, dedicated ARM64 Lima VM `clab-vrt-20260925-145115` with 8 CPUs, 16 GiB RAM, and 80 GiB disk. No pre-existing VM or container was used or modified. The qualified inputs were:

- Containerlab `0.79.0` at `5ae50094a3afd70e4e1674fe5385e64d8979da26`.
- Nokia SR Linux `25.10.1@sha256:bc8112667b5a87bee5039ade65b504ac2ef35511210d0675db6c7b0754e8cc4c` as `nokia_srlinux` type `ixr-d2`.
- VRT baseline `dbe85d37155145842da60367af1c4beef8801b0c` with the project-owned SDR migration and a normal-progress controller-retention repair.
- GraphX behavioral reference `7cad4da8646eda302a005070228495c1aa87d89a`.
- Final application image `sha256:39d88ab4eb62` (full local image ID recorded by Docker in the dedicated VM).

The retention repair calls controller expiry from ordinary `VitaRuntime::progress()`. A simulated-time regression completes and releases 300 transactions, exceeding the 256-record registry. Image `sha256:c408b1c9313535f22a7fcf48fe0d4b474d8084b8dfb30573db49dd3651be730c` then sustained 331 seconds and more than 256 one-second VRT liveness transactions with zero `vrt_status_failed`, retry-limit, or capacity events; all four radios remained active, generation 1, ready, and streaming. The final image adds only cooperative SIGINT/SIGTERM handling to the processor and detector loops; after coordinated startup, both stopped in 0.05 seconds.

## Acceptance status

| Requirement | Implementation | Verification evidence | Status |
| --- | --- | --- | --- |
| Independent application image | Multi-stage pinned `Dockerfile`; no graphx-docker build input | Final ARM64 image built as `sha256:39d88ab4eb62`; post-build checks found all four commands and no daemon socket | PASS |
| Four image commands | CMake install targets include radio, processor, detector, and recorder | All commands ran in the deployed lab; local CTest 12/12 | PASS |
| Reproducible dependency inputs | Immutable base digest, dated snapshots, release checksum, source revisions | Manifest syntax and pin checks passed | Verified statically |
| Deterministic configuration | Generator hash manifest and `--check` deployment gate | Generator tests 4/4 and freshness check passed; generated topology deployed | PASS |
| Scoped deploy/destroy | Exact topology, names, partial-failure cleanup, residual audit | Repeated exact destroy/deploy cycles passed without global cleanup | PASS |
| Bounded inspection/status | Exact containers, 40-line log tails, 2-second/4096-byte status client | All four management responses were bounded and reported ready/streaming | PASS |
| Bounded diagnostic capture | Fixed recorder/interface/filter; 60-second and 64 MiB bounds | Capture finalized within both bounds, but recorder reported 10,928 kernel drops | FAIL: completeness |
| SR Linux forwarding/mirroring/MTU | Native MAC-VRF and local mirror destination | Ports 1-6 and MAC-VRF up at L2 MTU 9000; mirror destination up; a mirrored IPv4 packet had length 9000, UDP payload 8972, DF set, and no fragments | PASS |
| Nominal 60-second loss gate | Direct IQ and spectrum counters | 60-second run had zero IQ packet gaps, spectrum sequence gaps, malformed packets, unavailable outputs, and send failures | PASS |
| Scheduled start and control liveness | Common epoch, VRT status every second, explicit handle release/expiry | Common start verified; 331-second final run passed beyond registry capacity with zero control failures | PASS |
| Detection output | Four independent FFT/detector paths | Streams 1-4 detected 100.0498046875, 100.10009765625, 100.14990234375, and 100.2001953125 MHz | PASS |
| Fault and restart behavior | Bounded reconnect, unavailable state, boot-ID reconciliation, SIGHUP re-exec | Ordinary reconnect, radio lifetime restart, missing-stream unavailable/recovery, and passive-recorder isolation were exercised; the full fault matrix was not exhausted | PARTIAL |
| Resource ceiling enforcement | 64 MiB app limits, unprivileged/no-restart nodes, bounded queues and capture | Effective Docker state verified; saturation, OOM, and all queue-pressure cases were not run | PARTIAL |
| Graceful shutdown | SIGINT/SIGTERM cooperatively stop processor and detector loops | Processor and detector each stopped in 0.05 seconds; radios and recorder had previously stopped in 0.06-0.15 seconds | PASS |
| Scoped cleanup proof | Exact-name and management-network audit | Intermediate cleanup audits passed; final cleanup recorded after qualification | PASS |

The diagnostic capture completeness gate remains failed because kernel drops make the saved capture incomplete. Exhaustive malformed-input, link-disable, saturation/OOM, and every restart race in the brief remain `NOT_RUN`; no acceptance claim is made for them.

## Exact commands

```sh
python3 scripts/generate_config.py --check
bash scripts/build-image.sh
sudo bash scripts/deploy.sh
bash scripts/inspect.sh
bash scripts/capture.sh
sudo bash scripts/destroy.sh
```

These commands are intended for a dedicated isolated Linux host with Docker and Containerlab. The integrated sequence above was executed in the dedicated Linux VM described here. Local source validation also passed 12/12 CTest targets, 4/4 Python generator tests, ShellCheck for every lifecycle script, and Python bytecode compilation. Final cleanup removed the eight exact containers and management network, and the dedicated VM was stopped.
