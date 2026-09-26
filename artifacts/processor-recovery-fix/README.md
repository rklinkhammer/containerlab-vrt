# Reproduce the processor recovery fix qualification

Local setup follows the project README. Verify the two-patch source before building:

```sh
python3 scripts/verify_vrt_source.py
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
```

For runtime qualification create a **new** dedicated ARM64 VM using docs/MACOS.md, wait for full READY, and copy the reviewed source including patched VRT headers into `$HOME/containerlab-vrt`. Copy this directory's guest-build.sh and trial.py into /tmp in the guest.

```sh
bash /tmp/guest-build.sh
python3 /tmp/trial.py
```

The first builds the pinned Release application image and a Debug test image and runs Linux tests. The second explicitly deploys the synthetic lab, replaces only processor through Containerlab, checks control and data recovery, and cleans up in finally. Check SUMMARY.json, not only process exit. Neither script creates a VM automatically.

Collect guest artifacts/processor-recovery-fix and audit the scoped cleanup before stopping the new VM from the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

If interrupted, run `sudo bash scripts/destroy.sh` in the original task guest repository before stopping where possible. Never reuse a stopped trial or prune Docker. Full JSONL logs remain local with reviewed snapshots and hashes. PLAN.md and the preserved earlier qualification define the unchanged acceptance expectations.
