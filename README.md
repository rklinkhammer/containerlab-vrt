# Four-radio SDR Containerlab lab

[Recorder recovery qualification](artifacts/recorder-recovery/RESULTS.md) passed: native replacement restored mirrored traffic, preserved the other seven nodes, and retained completed capture files. Capture attempts now use separate directories and manifests; interruptions remain explicit. Both captures hit byte limits and reported drops, so this does not qualify lossless recording.

[Health recovery qualification](artifacts/health-recovery/RESULTS.md) passed: the replacement processor recovered from degraded to healthy local activity while retaining 145 historical context-wait discards. Both local and Linux test suites pass; the dedicated VM is stopped.

Processor transition discards now have [explicit reasons](docs/DISCARD_DIAGNOSTICS.md). The [fresh replacement trial](artifacts/discard-classification/RESULTS.md) recorded 141 signals waiting for context, no other discard reasons, and no growth during sustained recovery. The legacy aggregate remains intact; [local health now recovers](docs/HEALTH_POLICY.md) after a quiet interval and fresh successful activity.

Standalone C++23 radio, processor, detector, and passive recorder applications for the generated Nokia SR Linux Containerlab topology. Containerlab owns topology and lifecycle. The project-owned VRT source under `third_party/vrt_framework` supplies the renamed SDR profile; graphx-docker is not a build or runtime dependency.

## Telemetry update

All four application roles now emit bounded `vrt.telemetry/1` JSON heartbeats. Configure `telemetry.interval_ms` in `config/lab.json` (default5000;0 disables new telemetry), then regenerate and rebuild. Detection events are rate limited; counters distinguish sequence gaps from proven loss and buffered PCAP writes from durable storage. See [contract and measurements](docs/TELEMETRY.md) and [actual qualification](artifacts/telemetry/RESULTS.md). No GUI, serial endpoint or new service is added.

## Current Linux qualification

The patched ARM64 images passed 15/15 Linux tests and a 340-second switched four-radio run, with all four tones detected and no observed sequence gaps. Qualification fixed a recorder startup race and test portability issues. Pause/resume recovers traffic, but direct Docker restart did not; the bounded capture also reached its size cap and measured kernel drops. See [results, image identity, failures and cleanup](artifacts/linux-runtime-qualification/RESULTS.md). The dedicated VM is stopped.

## Recovery

[Detector and single-radio replacement through Containerlab](docs/RECOVERY.md) are qualified on ARM64: each native workflow restored traffic while preserving the other seven node identities. Radio recovery included controller reconfiguration and a new start epoch; retries and sequence discontinuities were observed. See [radio evidence](artifacts/radio-recovery-qualification/RESULTS.md). Full-lab redeployment is a verified disruptive fallback. Link-only reconciliation after Docker restart did not restore the exec-configured address/MTU. See [recovery evidence](artifacts/recovery-qualification/RESULTS.md). [Processor replacement now passes on the updated images](artifacts/processor-recovery-fix/RESULTS.md): the command-resumption fix preserves stream IDs and restores controller and data operation. Upgrade radios and processor together. Native stop/reconfigure/start remains disruptive; [original failures](artifacts/processor-recovery-qualification/RESULTS.md) are preserved.

## Start here

**macOS:** follow [the Apple Silicon / Lima guide](docs/MACOS.md). Build and deploy inside the dedicated Linux guest. Docker Desktop and OrbStack are not required; their images and containers are separate from the guest's Docker daemon.

**Linux:** use an isolated host with Docker/BuildKit, Containerlab, Python 3.11+, and root privileges for link creation. The pinned SR Linux image must support the host architecture. Follow the workflow below.

**Current runtime fixes:** 15/15 native tests pass; the retention and coordinated-start defects are repaired by an explicit patch against the pinned VRT framework. See [causes, verification and reproduction](artifacts/runtime-fixes/RESULTS.md). Historical VM and integrated qualification remains historical; it does not qualify the patched image.

No lifecycle script manages Lima. No application container receives a Docker or Podman daemon socket. Containerlab and these lifecycle scripts must execute on the Linux host that owns the lab; selecting a remote Docker context on a Mac is insufficient.

## Run the Containerlab example

Run the example on an isolated Linux host that meets the prerequisites above. From a fresh checkout, initialize the VRT framework submodule first:

```sh
git submodule update --init --recursive
python3 scripts/verify_vrt_source.py --apply
```

Generate the topology and application configuration first (`generated/` is not shipped in a fresh clone), verify them, then build the local application image:

```sh
python3 scripts/generate_config.py
python3 scripts/generate_config.py --check
sudo bash scripts/build-image.sh
```

The build creates `containerlab-vrt-app:local`. The qualified Lima recipe uses `sudo` for Docker socket access. On another Linux host where the current user already has Docker access, `sudo` may be omitted.

Deploy the eight-node lab:

```sh
sudo bash scripts/deploy.sh
```

Deployment creates four radio containers, the processor, detector, passive recorder, and Nokia SR Linux switch. Containerlab creates the seven application links and the separate Docker management network. The script refuses to replace an existing lab with the same names.

Inspect readiness, radio status, recent application output, and SR Linux interface, MAC-VRF, and mirroring state:

```sh
sudo bash scripts/inspect.sh
```

The processor coordinates a common scheduled start. To follow the main data path after deployment:

```sh
sudo docker logs --follow clab-four-radio-sdr-processor
# After Ctrl-C, or in a separate Linux-guest terminal:
sudo docker logs --follow clab-four-radio-sdr-detector
```

The processor emits periodic JSON loss and spectrum counters. The detector emits JSON detections for streams 1-4 near 100.050, 100.100, 100.150, and 100.200 MHz. Stop log following with `Ctrl-C`; this does not stop the lab.

Optionally collect the bounded 60-second mirrored-frame capture:

```sh
sudo bash scripts/capture.sh
```

Capture output and its metrics are written under ignored `artifacts/runtime/`. Kernel-drop counters must be checked before treating a capture as complete.

Destroy only this lab's resources when finished:

```sh
sudo bash scripts/destroy.sh
```

The destroy script removes the generated topology's eight exact containers and management network, then audits for residual task-owned resources. It does not prune unrelated Docker or Containerlab resources.

## Build details

The image build uses `container/dependencies.env`, the immutable Debian base digest, dated Debian snapshots, and the checksum-verified nlohmann/json release. It builds `radio`, `processor`, `detector`, and `recorder` from this repository without graphx-docker. The post-build check runs with `--network none` and verifies all four commands and daemon-socket absence.

## Lifecycle details

The scripts operate only on lab `four-radio-sdr`, network `four-radio-sdr-mgmt`, and the eight exact `clab-four-radio-sdr-*` container names. Deploy refuses existing names and cleans a partial deployment through the same generated topology. Destroy audits those exact names and never prunes globally.

Capture is fixed to all mirrored frames on recorder `eth1`, at most 60 seconds, and at most 64 MiB. It accepts no caller-provided filter. Each attempt writes a separate session under ignored `artifacts/runtime/captures/` in the Linux checkout, with a manifest containing exact container identity, timestamps and file SHA-256 values. Previous files are retained; run one capture at a time to avoid competing for receive resources. Interrupted attempts remain explicitly incomplete. See [preservation semantics](docs/CAPTURE_PRESERVATION.md). A zero exit status does not establish capture completeness; inspect the drop counters. See the macOS guide for copying results to the Mac.

## Configuration

Edit `config/lab.json`, then regenerate and review:

```sh
python3 scripts/generate_config.py
python3 scripts/generate_config.py --check
```

`generated/manifest.json` binds generated topology and application configuration to the parameter source and generator hashes. Deployment rejects stale generated files. The local application image tag is intentionally stable; rebuild it after source or dependency changes.

## Historical integrated qualification

The earlier integrated ARM64 lab was built and deployed in a dedicated Lima VM with Containerlab `0.79.0` and Nokia SR Linux `25.10.1` at its recorded immutable digest. Eight nodes and seven physical links ran through the native SR Linux MAC-VRF and local mirror destination.

The 331-second sustained run crossed the VRT controller's 256-record threshold with zero control failures after adding normal-progress transaction expiry. All four radios remained ready and streaming; processor and detector counters reported zero gaps, malformed packets, unavailable outputs, or send failures, and all four configured tones were detected. The final image also stopped processor and detector cooperatively in 0.05 seconds each. That historical source state passed 12/12 CTest targets, 4/4 Python tests, ShellCheck, and Python compilation; final scoped cleanup passed and the dedicated VM was stopped.

Qualification is not universal. The bounded 60-second diagnostic capture finalized but reported 10,928 kernel drops, so capture completeness is `FAIL`. The exercised fault subset passed, but the brief's exhaustive malformed-input, link-disable, saturation/OOM, and restart-race matrix remains `NOT_RUN`. See `IMPLEMENTATION.md` for exact pins, evidence, and the acceptance table.
