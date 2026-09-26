# Reproduce radio replacement qualification

Use a new dedicated ARM64 Lima guest with the pinned template documented in docs/MACOS.md. Wait for complete READY/provisioning, not SSH alone. Never reuse a prior trial VM.

Copy the reviewed source (including the patched VRT include tree) into `$HOME/containerlab-vrt`. Copy guest-build.sh and trial.py from this directory into `/tmp` in that guest.

```sh
bash /tmp/guest-build.sh
python3 /tmp/trial.py
```

The explicit runtime trial deploys the generated eight-node synthetic lab, establishes a baseline, removes radio1 through native filtered destruction, deploys the full unchanged topology, measures recovery, stops the processor to collect final controller metrics, and destroys the lab in a finally block. It is not an ordinary test and does not manage Lima.

Inspect every case in trial/SUMMARY.json; the harness process exit code alone is not acceptance. Preserve failed attempts. Collect guest artifacts/radio-recovery-qualification, audit scoped cleanup and remaining containers, then on the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

If interrupted, run `sudo bash scripts/destroy.sh` in the original task guest repository before stopping the VM where possible. Do not prune Docker or access other VMs. Keep full JSONL logs local, retain their hashes and reviewed snapshots. See PLAN.md for independent expectations and SOURCE_INSPECTION.md for evidence limits.
