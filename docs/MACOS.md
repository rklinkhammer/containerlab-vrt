# macOS: build and run in a dedicated Linux VM

## What runs where

| Mac terminal | Linux guest (`~/containerlab-vrt`) |
| --- | --- |
| Lima management, editor, copying results | Docker build, Containerlab, lab scripts and application containers |
| `limactl ...` | `sudo bash scripts/...` and `sudo docker ...` |

The recorded environment uses Apple Silicon and Lima 2.2.0. The recipe pins an ARM64 Ubuntu 26.04 image and allocates 8 CPUs, 16 GiB RAM, and an 80 GiB virtual disk. Ensure the Mac has capacity. Intel Macs need a separately qualified AMD64 recipe; do not use this ARM64 recipe as an Intel qualification.

The guest has **no host filesystem mounts**, forwarded SSH agent, or forwarded application ports. Editing the Mac checkout does not update the guest checkout. Docker Desktop/OrbStack images are not available in this guest. No browser GUI is provided by this project.

Current native fixes are in [runtime fix evidence](../artifacts/runtime-fixes/RESULTS.md); historical deployment limitations are in [the review](../artifacts/review/REVIEW-20260925.md). After initializing submodules, run `python3 scripts/verify_vrt_source.py --apply` before building, including in a fresh guest checkout. The following is a reviewed setup procedure; a fresh VM installation was not rerun during that review.

## 1. Create the VM — Mac terminal, repository root

Install Lima if absent:

```sh
brew install lima
limactl --version
uname -m
```

Expect `arm64` on the Mac. Choose a new dedicated instance. Keep its name in an ignored file so another terminal can recover it. Finish cleanup of any previous instance referenced by this file before replacing it.

```sh
mkdir -p artifacts/runtime
VM_NAME="clab-vrt-$(date +%Y%m%d-%H%M%S)"
printf '%s\n' "$VM_NAME" > artifacts/runtime/vm-name
limactl start --name="$VM_NAME" --tty=false --timeout=15m \
  artifacts/environment/lima-clab-vrt-20260925-145115.yaml
```

In each additional **Mac** terminal, return to this repository and restore the name:

```sh
VM_NAME=$(cat artifacts/runtime/vm-name)
```

Do not substitute an unrelated existing instance. If provisioning fails, inspect that instance's error and stop that exact instance; do not prune other VMs.

## 2. Install Containerlab — Mac terminal

The recipe installs Docker. Install Git, Python and the pinned Containerlab release in the guest; verify the downloaded archive before installing:

```sh
limactl shell "$VM_NAME" -- bash -lc '
  set -euo pipefail
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends git python3
  work=$(mktemp -d)
  trap '\''rm -rf "$work"'\'' EXIT
  cd "$work"
  curl -fLO https://github.com/srl-labs/containerlab/releases/download/v0.79.0/checksums.txt
  curl -fLO https://github.com/srl-labs/containerlab/releases/download/v0.79.0/containerlab_0.79.0_linux_arm64.tar.gz
  grep " containerlab_0.79.0_linux_arm64.tar.gz$" checksums.txt | sha256sum -c -
  tar -xzf containerlab_0.79.0_linux_arm64.tar.gz containerlab
  sudo install -m 0755 containerlab /usr/local/bin/containerlab
  containerlab version
  sudo docker version
'
```

## 3. Clone and enter the guest — Mac terminal

```sh
limactl shell "$VM_NAME" -- bash -lc '
  set -euo pipefail
  git clone --recurse-submodules https://github.com/rklinkhammer/containerlab-vrt.git ~/containerlab-vrt
  cd ~/containerlab-vrt
  git rev-parse HEAD
  git submodule status
'
limactl shell "$VM_NAME" -- bash -lc 'cd ~/containerlab-vrt && exec bash -i'
```

This clones the published repository, not uncommitted or unpublished Mac changes. To reproduce a particular revision, check out its published commit in the guest and run `git submodule update --init --recursive` before building. Record both revisions; do not assume the image's historical VRT label proves its source version.

## 4. Generate, build and deploy — Linux guest terminal

```sh
pwd
python3 scripts/generate_config.py
python3 scripts/generate_config.py --check
sudo bash scripts/build-image.sh
sudo bash scripts/deploy.sh
sudo bash scripts/inspect.sh
```

The image is built in the same Docker daemon used by Containerlab. Network access is required for initial package/image downloads. Do not deploy if generation or build fails. Preserve the deployed generated topology until cleanup; do not regenerate it to describe another lab while this lab is running.

There are eight nodes: four radios, processor, detector, recorder and SR Linux switch. Inspection checks readiness and reports recent logs. A successful deploy alone does not prove successful SDR streaming.

## 5. Follow logs — separate Mac terminals

Restore `VM_NAME` in each terminal as shown above. These commands block while following logs; run one per terminal, or press Ctrl-C before switching:

```sh
limactl shell "$VM_NAME" -- sudo docker logs --tail 40 --follow clab-four-radio-sdr-processor
```

```sh
limactl shell "$VM_NAME" -- sudo docker logs --tail 40 --follow clab-four-radio-sdr-detector
```

Ctrl-C stops log following, not the lab. For a snapshot, omit `--follow`. Expect periodic processor counters and detections for streams 1–4; startup/control failures require investigation, not repeated deployment over an existing lab.

## 6. Capture and retrieve evidence

**Linux guest**, run one capture at a time:

```sh
sudo bash scripts/capture.sh
```

Each attempt creates a unique directory under the guest's `artifacts/runtime/captures/` and prints `capture_directory=...`. Previous sessions are retained. A manifest records the exact container ID, timestamps, file hashes and completed/incomplete status. The diagnostic capture is limited to 60 seconds and 64 MiB. Examine metrics for kernel drops; a completed command or a file reaching its size limit is not proof of a complete 60-second capture. Historical qualification had nonzero drops.

**Mac terminal**, copy results before cleanup:

```sh
VM_NAME=$(cat artifacts/runtime/vm-name)
GUEST_REPO=$(limactl shell "$VM_NAME" -- bash -lc 'printf "%s/containerlab-vrt" "$HOME"')
DEST="artifacts/runtime/$VM_NAME/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$DEST"
# Set this to the capture directory printed by the guest command.
CAPTURE_SESSION=capture-REPLACE_WITH_PRINTED_SUFFIX
limactl copy --backend=scp -r \
  "$VM_NAME:$GUEST_REPO/artifacts/runtime/captures/$CAPTURE_SESSION" "$DEST/"
cat "$DEST/$CAPTURE_SESSION/manifest.json"
cat "$DEST/$CAPTURE_SESSION/metrics.json"
shasum -a 256 "$DEST/$CAPTURE_SESSION/capture.pcap"
```

Compare the PCAP hash with manifest.json. These files now reside on the Mac. Raw captures remain ignored by Git. `completed` means the bounded command finished and copied its finalized files; it does not establish lossless capture or durable storage. Failed/interrupted attempts retain an `incomplete` manifest and any copied partial files. In-container partials may be unavailable after replacement. Only files copied outside the container survive its removal. See [capture preservation](CAPTURE_PRESERVATION.md).

## 7. Clean up — Mac terminal

After saving evidence, destroy the lab in its original guest checkout, then stop the exact VM:

```sh
VM_NAME=$(cat artifacts/runtime/vm-name)
limactl shell "$VM_NAME" -- bash -lc 'cd ~/containerlab-vrt && sudo bash scripts/destroy.sh'
limactl shell "$VM_NAME" -- sudo docker ps -a
limactl stop "$VM_NAME"
limactl list
```

Read the destroy audit and investigate any residual resources. If destruction fails, stopping the VM still stops its workload but does not remove containers from its disk; report that distinction. `exit` merely leaves a guest shell. `limactl stop` preserves the VM disk and guest files. No VM disk deletion or global Docker prune is required by this workflow.

## Troubleshooting

| Symptom | Meaning / action |
| --- | --- |
| `containerlab: command not found` on Mac | Run lifecycle scripts inside the Linux guest. |
| Docker socket permission denied in guest | Use the documented `sudo` commands; do not make the socket world-writable. |
| Generated configuration missing/stale | Run the generator, then `--check`, before initial build/deploy. |
| Application image absent during deploy | Build in the guest; a Docker Desktop build is in a different daemon. |
| Existing lab names rejected | Inspect and clean up the previous owned lab; deploy deliberately refuses replacement. |
| Second log command never starts | The first `--follow` blocks. Use another terminal or Ctrl-C. |
| Capture missing on Mac | It is initially in the guest checkout; use the copy step. |
| Build passes but control tests fail | Verify the pinned framework patch with `python3 scripts/verify_vrt_source.py`; see runtime fix evidence. Preserve any new failure instead of relying on historical qualification. |

## Current patched-image qualification

The fresh ARM64 run is recorded in [Linux qualification results](../artifacts/linux-runtime-qualification/RESULTS.md). Both the unit/integration suite and sustained switched traffic passed. The recorder now waits for its interface to become administratively up before opening its packet socket. Do not use a bare Docker stop/start as a qualified Containerlab node recovery procedure: that trial restarted the detector process but did not restore its traffic. Subsequent [detector and single-radio replacement workflows](RECOVERY.md) are qualified through native filtered destruction and full-topology deployment; processor replacement subsequently passed with the [command-resumption fix](../artifacts/processor-recovery-fix/RESULTS.md), after the original controller-recovery failure. Upgrade radios and processor together. [Recorder replacement and completed capture preservation](../artifacts/recorder-recovery/RESULTS.md) are now qualified; switch recovery remains untested. Full native redeploy is the disruptive fallback.

## Telemetry qualification

The new stdout telemetry is described in [TELEMETRY.md](TELEMETRY.md). Local C++ and process checks run on macOS; AF_PACKET recorder checks require Linux. Use [the explicit telemetry qualification](../artifacts/telemetry/README.md) only in a newly created guest. Its finite isolated containers need no Containerlab deployment or SR Linux image; they do not establish full workflow health. Collect image ID/results and stop that exact VM afterward. Existing node logs can display the JSON records without serial ports.
