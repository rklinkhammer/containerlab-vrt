cd # VRT runtime failure resolution

## Scope and causes

**Observed:** the pre-telemetry baseline failed transaction retention and coordinated start (see `../telemetry/baseline-ctest.log`). Existing acceptance expectations were retained.

**Documented (pinned source):** `VitaRuntime::progress()` did not expire the controller registry or shared retention store; expiry was called only during stream reuse. Released, expired transactions therefore accumulated until the bounded registry filled. The patch runs both expiry operations during normal serialized progress. Existing expiry rules still protect active transactions, retained handles and replay windows.

**Documented (pinned source):** an SDR start uses an absolute protocol epoch while the source is stopped. PPS updates change clock mapping generation. At execution, the old mapping caused the scheduler to reselect the admitted boundary with an admission preparation lead, although that epoch was now due. The patch preserves the admitted absolute epoch across qualified clock remapping; it retains clock qualification and actual-effect timing-window checks. It does not widen timing tolerances.

**Observed:** both existing failing tests pass with these changes. The coordinated controller test also passes three consecutive runs, including first-sample epoch equality and sustained control operation (`controller-repeat.log`). A new independent injected-clock test verifies admission, no early activation, successful start after four intervening PPS updates, and rejection at 10 ms late with the original timing limits. Compiling that same test against the unpatched engine fails at the required activation assertion (`unpatched-regression.log`).

## Reproducibility and provenance

Upstream base: `51853ba29703f51aceb2cfefe5a12a65a8e1110f`.
Local changes: `patches/vrt-runtime-progress.patch` (two headers only).
`container/vrt-source.json` records the upstream include-tree hash, patch SHA-256 and patched include-tree hash. The upstream commit label is a base identity, not a claim that upstream includes these repairs. The Docker image carries the manifest and patch in `/usr/share/doc/containerlab-vrt/`.

From repository root on the existing macOS development toolchain:

```sh
git submodule update --init --recursive
python3 scripts/verify_vrt_source.py --apply
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev -j 6
ctest --test-dir build/dev --output-on-failure
```

`--apply` is idempotent and refuses unknown/modified header trees. The normal verifier is read-only. Patch reproduction from the two original committed headers was checked in a disposable directory; both resulting files matched this checkout byte for byte. The submodule intentionally has local modifications; commit the parent-owned patch and manifest, not a fictional upstream revision.

## Verification and limits

- **Observed: 15/15 native CTest targets pass** (17.53 seconds, macOS ARM64 Debug). See `ctest.log` and `build.log`.
- `ctest-first.log` preserves an unsuccessful new-test attempt: it inspected the latest response rather than requesting execution evidence. The corrected test uses the public `wait(..., WaitEvidence::execution/validation)` API. Success expectations were unchanged.
- Source integrity verification and `git diff --check` pass.
- New Linux image build, dedicated-VM deployment, complete Containerlab workflow, and upstream's full test suite: **not run in this repair**. Prior Linux telemetry images do not contain this patch and must be rebuilt before further qualification.
- No VM, container or lab was accessed, created or changed. Disposable patch/probe directories were removed.

Next: rebuild the patched image in a fresh dedicated VM and qualify the complete coordinated workflow with telemetry enabled, then clean up the task lab and stop that VM. Native success does not replace that integration evidence.
