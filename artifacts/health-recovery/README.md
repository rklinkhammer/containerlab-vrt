# Reproduce local health qualification

Local setup follows the project README:

```sh
python3 scripts/verify_vrt_source.py
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

Create a NEW dedicated ARM64 VM per docs/MACOS.md; wait for full READY. Copy reviewed source including patched headers into `$HOME/containerlab-vrt`, and this directory's guest-build.sh and trial.py into /tmp.

```sh
bash /tmp/guest-build.sh
python3 /tmp/trial.py
```

The explicit script builds/tests Linux images and runs a bounded synthetic processor replacement with scoped finally cleanup. Ordinary tests never create VMs. Copy guest artifacts/health-recovery into this directory's guest/ and run `python3 artifacts/health-recovery/audit_health.py` on the host for independent health/counter checks.

Audit exact lab cleanup and remaining containers, then from the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

If interrupted, run `sudo bash scripts/destroy.sh` in the original task guest checkout before stopping where possible. Never reuse stopped trials, access other VMs or prune Docker. Keep JSONL local with hashes and reviewed summaries. PLAN.md records expectations; ../../docs/HEALTH_POLICY.md defines the contract.
