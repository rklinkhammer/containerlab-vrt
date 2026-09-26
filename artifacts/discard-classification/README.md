# Reproduce discard classification

Local build and tests:

```sh
python3 scripts/verify_vrt_source.py
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

Runtime qualification requires an explicitly created NEW dedicated ARM64 VM using docs/MACOS.md. Wait for full READY, copy reviewed source including patched headers into `$HOME/containerlab-vrt`, and copy this directory's guest-build.sh and trial.py into /tmp.

```sh
bash /tmp/guest-build.sh
python3 /tmp/trial.py
```

The build script runs Linux tests. The explicit trial deploys the synthetic lab, replaces processor, checks both controller and traffic recovery, and destroys the lab in finally. It does not manage Lima or run as part of ordinary tests. Check every SUMMARY.json case; process exit alone is insufficient.

Copy guest artifacts/discard-classification and audit scoped cleanup, then from the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

On interruption run `sudo bash scripts/destroy.sh` in the original task guest repository before stopping where possible. Do not reuse stopped trials, access existing VMs or prune Docker. Full synthetic JSONL remains local; retain reason summaries and hashes. PLAN.md defines independent expectations; ../../docs/DISCARD_DIAGNOSTICS.md defines the contract.
