# Four-radio SDR Containerlab lab

Standalone C++23 radio, processor, detector, and passive recorder applications for the generated Nokia SR Linux Containerlab topology. Containerlab owns topology and lifecycle. The project-owned VRT source under `third_party/vrt_framework` supplies the renamed SDR profile; graphx-docker is not a build or runtime dependency.

## Prerequisites

- Docker with BuildKit
- Containerlab compatible with the generated topology
- Python 3.11 or newer
- Root privileges for Containerlab link creation
- Host architecture supported by the pinned SR Linux image

No script starts or modifies a Lima VM. No container receives a Docker or Podman daemon socket.

## Build and static checks

```sh
python3 scripts/generate_config.py --check
bash scripts/build-image.sh
```

The image build uses `container/dependencies.env`, the immutable Debian base digest, dated Debian snapshots, and the checksum-verified nlohmann/json release. It builds `radio`, `processor`, `detector`, and `recorder` from this repository without graphx-docker. The post-build check runs with `--network none` and verifies all four commands and daemon-socket absence.

## Lifecycle

```sh
sudo bash scripts/deploy.sh
bash scripts/inspect.sh
bash scripts/capture.sh
sudo bash scripts/destroy.sh
```

The scripts operate only on lab `four-radio-sdr`, network `four-radio-sdr-mgmt`, and the eight exact `clab-four-radio-sdr-*` container names. Deploy refuses existing names and cleans a partial deployment through the same generated topology. Destroy audits those exact names and never prunes globally.

Capture is fixed to all mirrored frames on recorder `eth1`, at most 60 seconds, and at most 64 MiB. It accepts no caller-provided filter. The retrieved file and SHA-256 are written under ignored `artifacts/runtime/`.

## Configuration

Edit `config/lab.json`, then regenerate and review:

```sh
python3 scripts/generate_config.py
python3 scripts/generate_config.py --check
```

`generated/manifest.json` binds generated topology and application configuration to the parameter source and generator hashes. Deployment rejects stale generated files. The local application image tag is intentionally stable; rebuild it after source or dependency changes.

## Qualification status

The integrated ARM64 lab was built and deployed in a dedicated Lima VM with Containerlab `0.79.0` and Nokia SR Linux `25.10.1` at its recorded immutable digest. Eight nodes and seven physical links ran through the native SR Linux MAC-VRF and local mirror destination.

The 331-second sustained run crossed the VRT controller's 256-record threshold with zero control failures after adding normal-progress transaction expiry. All four radios remained ready and streaming; processor and detector counters reported zero gaps, malformed packets, unavailable outputs, or send failures, and all four configured tones were detected. The final image also stopped processor and detector cooperatively in 0.05 seconds each. Local validation passes 12/12 CTest targets, 4/4 Python tests, ShellCheck, and Python compilation; final scoped cleanup passed and the dedicated VM was stopped.

Qualification is not universal. The bounded 60-second diagnostic capture finalized but reported 10,928 kernel drops, so capture completeness is `FAIL`. The exercised fault subset passed, but the brief's exhaustive malformed-input, link-disable, saturation/OOM, and restart-race matrix remains `NOT_RUN`. See `IMPLEMENTATION.md` for exact pins, evidence, and the acceptance table.
