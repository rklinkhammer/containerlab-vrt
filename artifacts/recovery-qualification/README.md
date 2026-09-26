# Reproduce recovery qualification

Create a **new** dedicated ARM64 VM using docs/MACOS.md, and wait for Lima's complete READY result (SSH readiness alone can precede package provisioning). Never reuse a stopped trial. Copy the reviewed source archive into `$HOME/containerlab-vrt`, including patched VRT headers. Copy this directory's guest-build.sh, trial.py and replacement_trial.py into /tmp in the guest.

```sh
bash /tmp/guest-build.sh
python3 /tmp/trial.py
# After its cleanup, run the independently scoped replacement case:
python3 /tmp/replacement_trial.py
```

The first command builds from pinned inputs and captures native version/help. The second is an explicitly destructive, bounded experiment on this guest's generated example lab: it creates the lab, interrupts its detector, tests native recovery, and destroys the lab in a finally block. It is not an ordinary test or a generic GUI endpoint.

Review SUMMARY.json, logs, interface snapshots, container/process identities and measured detector progress together. The initial and successful recovered phases each include 30 seconds of increasing traffic. Dry-run output is not runtime evidence. A full redeploy is a disruptive fallback, never an isolated-recovery pass.

Copy guest `artifacts/recovery-qualification/` before stopping the VM. Keep full synthetic JSON logs local and retain their hashes with reviewed snapshots. Check the scoped cleanup log and exact residual resources. From the Mac:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

No script automatically creates a VM, accesses other VMs, or constructs links outside Containerlab. If interrupted, run the existing scripts/destroy.sh against the original guest topology before stopping where possible. Do not use Docker prune.
