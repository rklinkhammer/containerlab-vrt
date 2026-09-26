# Reproduce the patched Linux qualification

This is an explicit runtime procedure, never part of ordinary tests. Create a **new** dedicated ARM64 VM using the pinned template in docs/MACOS.md. Never reuse a stopped trial. Copy the current source plus verified VRT headers, this directory's `guest-build.sh`, `runtime_trial.py` and `overhead.py` to that guest. Record the actual source commit, uncommitted diff, patch hashes and archive hash. Do not clone an older published tree and assume it matches local changes.

The scripts expect the source at `$HOME/containerlab-vrt` in the guest:

```sh
bash /tmp/guest-build.sh
python3 /tmp/runtime_trial.py
```

The build script pins Containerlab and package/image inputs through the repository recipes. It configures bounded Docker logging in the fresh guest daemon, builds the production image and a Debug test image, runs CTest, and exercises finite isolated telemetry cases. The runtime script deploys the exact reviewed generated topology, collects a 340-second sustained window and bounded recorder capture, tests detector pause/resume and traffic interruption, and destroys its exact lab in a finally block. Inspect per-case evidence even if the process returns zero; aggregate fidelity and health claims require analysis.

After the traffic trial, run `bash /tmp/guest-benchmark.sh`; copy that script and overhead.py into /tmp first. It builds a test-only Python layer from the same pinned Debian snapshot and runs five interleaved samples per interval plus process tests. Run `python3 /tmp/link_readiness.py` for the delayed-up/never-up recorder regressions. Preserve their outputs. Do not overlap this benchmark with the switched traffic trial.

Copy reviewed evidence from guest `artifacts/linux-runtime-qualification/` and `artifacts/telemetry/linux-results.json`; keep raw PCAP files under ignored `artifacts/runtime/`. If any script fails, preserve its logs before retrying. Collect image IDs, resource states and cleanup output.

Finally, from the Mac, stop **only the new VM name recorded for this trial**:

```sh
limactl stop "$VM_NAME"
limactl list "$VM_NAME" --json
```

An interrupted trial requires scoped `scripts/destroy.sh` cleanup before stopping where possible. A stopped VM without a successful destruction audit is not proof that its disk contains no containers.

The original sustained attempt used runtime_trial_initial.py. The current harness selects heartbeat measurements separately from detection events and replaces bare Docker restart with pause/resume. `--fault-only` runs a separate interruption trial and explicitly establishes no sustained-duration result. See RESULTS.md for the failed direct-restart recovery; it is not silently counted as a pass.
