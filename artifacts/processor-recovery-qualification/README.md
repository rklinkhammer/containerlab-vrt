# Reproduce processor recovery qualification

Create a new dedicated ARM64 guest using the pinned template documented in docs/MACOS.md. Never reuse a stopped trial. Wait for complete Lima READY. Copy reviewed source (including patched VRT headers) into `$HOME/containerlab-vrt`, and this directory's guest-build.sh and trial.py into /tmp.

```sh
bash /tmp/guest-build.sh
python3 /tmp/trial.py
```

This explicit runtime experiment deploys the eight-node synthetic lab, establishes traffic, replaces only processor through Containerlab, verifies fresh traffic and controller/radio identities, stops the processor to collect metrics and destroys the exact lab in a finally block. It is not an ordinary test. Acceptance requires checking all SUMMARY.json fields, not just the Python exit code.

Copy guest artifacts/processor-recovery-qualification before stopping. Keep raw JSONL local, preserve hashes and reviewed snapshots. Audit cleanup and remaining containers. On the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

On interruption, run `sudo bash scripts/destroy.sh` in the original task guest repository before stopping where possible. Do not prune Docker, repair links manually, or access other VMs. PLAN.md defines the acceptance expectations; SOURCE_INSPECTION.md separates source evidence from runtime claims.
