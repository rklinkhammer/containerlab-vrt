# Reproduce recorder recovery qualification

Ordinary local checks (no Docker or VM creation):

```sh
python3 scripts/verify_vrt_source.py
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
python3 tests/capture_session_tests.py
```

Follow ../../docs/MACOS.md to create a NEW dedicated ARM64 VM. Never reuse stopped trials. Copy the reviewed source snapshot including both pinned native patches and headers to `$HOME/containerlab-vrt`; copy guest-build.sh and trial.py to /tmp. The snapshot's SHA-256 and VRT manifest are in session.json. Then explicitly run in that fresh guest:

```sh
bash /tmp/guest-build.sh
sudo docker pull ghcr.io/nokia/srlinux:25.10.1@sha256:bc8112667b5a87bee5039ade65b504ac2ef35511210d0675db6c7b0754e8cc4c
cd ~/containerlab-vrt
python3 tests/capture_session_tests.py
python3 /tmp/trial.py
```

trial.py is fixture-specific qualification, not generic orchestration. PLAN.md defines its independent expectations. It runs exact native recorder replacement, interrupted and completed captures, structural PCAP audits, identity and traffic assertions, and scoped lab cleanup in finally. It records failures rather than retrying with modified expectations. PCAPs remain ignored in guest artifacts/runtime/captures; manifests/audits contain hashes and metrics.

Copy artifacts/recorder-recovery from the guest to this directory's guest/. Preserve raw local logs with hashes; do not commit packet captures. Then stop only the newly created VM from the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

If interrupted, run `sudo bash scripts/destroy.sh` inside the original task guest checkout before stopping when possible. Never prune Docker globally or delete other VM disks. A stopped VM retains its guest disk/captures, but it must not be reused for another qualification.
