# Four-radio SDR Containerlab lab

Standalone C++23 radio, processor, detector, and passive recorder applications for the generated Nokia SR Linux Containerlab topology. Containerlab owns topology and lifecycle. The project-owned VRT source under `third_party/vrt_framework` supplies the renamed SDR profile; graphx-docker is not a build or runtime dependency.

## Prerequisites

- Docker with BuildKit
- Containerlab compatible with the generated topology
- Python 3.11 or newer
- Root privileges for Containerlab link creation
- Host architecture supported by the pinned SR Linux image

No script starts or modifies a Lima VM. No container receives a Docker or Podman daemon socket.

Containerlab deployment must run on Linux. Docker Desktop or OrbStack on macOS can build `containerlab-vrt-app:local`, but a successful dashboard build does not provide the Linux host networking or `containerlab` executable required by `scripts/deploy.sh`. Merely selecting a VM-backed Docker context is insufficient because the lifecycle scripts invoke `containerlab` and configure links on their local host.

## macOS setup with Lima

The qualified macOS environment is Apple Silicon with Lima 2.2.0. The checked-in VM recipe uses a checksum-pinned ARM64 Ubuntu image, 8 CPUs, 16 GiB RAM, an 80 GiB disk, no host mounts, and no forwarded SSH agent. Intel Macs require a separate AMD64 recipe and have not been qualified for this lab.

Install Lima on the Mac, choose a new VM name, and create the dedicated Linux VM from the repository root:

```sh
brew install lima
VM_NAME="clab-vrt-$(date +%Y%m%d-%H%M%S)"
limactl start --name="$VM_NAME" --tty=false --timeout=15m \
  artifacts/environment/lima-clab-vrt-20260925-145115.yaml
```

The recipe installs and starts Docker. Install Git, Python, and the checksum-verified Containerlab 0.79.0 ARM64 release inside that new VM:

```sh
limactl shell "$VM_NAME" -- bash -lc '
  set -euo pipefail
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends git python3
  cd /tmp
  curl -fLO https://github.com/srl-labs/containerlab/releases/download/v0.79.0/checksums.txt
  curl -fLO https://github.com/srl-labs/containerlab/releases/download/v0.79.0/containerlab_0.79.0_linux_arm64.tar.gz
  grep " containerlab_0.79.0_linux_arm64.tar.gz$" checksums.txt | sha256sum -c -
  tar -xzf containerlab_0.79.0_linux_arm64.tar.gz containerlab
  sudo install -m 0755 containerlab /usr/local/bin/containerlab
  containerlab version
'
```

Clone the project and its VRT submodule into the VM, then enter the VM checkout:

```sh
limactl shell "$VM_NAME" -- bash -lc '
  git clone --recurse-submodules https://github.com/rklinkhammer/containerlab-vrt.git ~/containerlab-vrt
'
limactl shell "$VM_NAME" -- bash -lc 'cd ~/containerlab-vrt && exec bash -i'
```

The interactive prompt should now be in `~/containerlab-vrt`. Run the Linux workflow below from that shell. After destroying the lab, leave the guest and stop only the VM created above:

```sh
exit
limactl stop "$VM_NAME"
```

Do not reuse or modify an unrelated Lima VM. Docker Desktop and OrbStack are not required for this Lima workflow.

## Run the Containerlab example

Run the example on an isolated Linux host that meets the prerequisites above. From a fresh checkout, initialize the VRT framework submodule first:

```sh
git submodule update --init --recursive
```

Verify the generated topology and application configuration, then build the local application image:

```sh
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
