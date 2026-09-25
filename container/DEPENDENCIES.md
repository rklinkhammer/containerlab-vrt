# Container dependency manifest

| Input | Pin | Purpose |
| --- | --- | --- |
| Debian | `bookworm-20250908-slim@sha256:df52e55e3361a81ac1bead266f3373ee55d29aa50cf0975d440c2be3483d8ed3` | Build and runtime base; Docker Hub manifest last updated 2025-09-08 |
| Debian archive | `20250908T000000Z` | Immutable package repository snapshot |
| Debian security archive | `20250908T000000Z` | Immutable security package repository snapshot |
| nlohmann/json | `3.12.0`, SHA-256 `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa` | JSON parsing and metrics |
| VRT framework | `51853ba29703f51aceb2cfefe5a12a65a8e1110f` | Upstream base plus `patches/vrt-runtime-progress.patch`, MIT |
| graphx-docker | `7cad4da8646eda302a005070228495c1aa87d89a` | Behavioral reference only, MIT; absent from build context and image |
| Nokia SR Linux | `25.10.1@sha256:bc8112667b5a87bee5039ade65b504ac2ef35511210d0675db6c7b0754e8cc4c` | Switch image, generated from `config/lab.json` |

The builder installs `ca-certificates`, `cmake`, `curl`, `g++`, `libsoapysdr-dev`, and `ninja-build` from the dated Debian snapshot. The runtime installs only `iproute2` and `libsoapysdr0.8` from the same snapshot. Snapshot pinning fixes transitive package resolution; `container/dependencies.env` is the machine-readable source for build arguments.

The application image itself is local and therefore has no repository digest before building. Record `docker image inspect containerlab-vrt-app:local --format '{{json .RepoDigests}} {{.Id}}'` with qualification evidence. A local image ID is build evidence, not a portable registry digest.

## Current source verification

VRT_REVISION identifies the upstream base; the source also requires the explicit local patch listed in `container/vrt-source.json`. After submodule initialization, run `python3 scripts/verify_vrt_source.py --apply`. This refuses unknown header trees and is idempotent on the verified patched tree. The manifest records the base hash, patch hash, and resulting include-tree hash. The patch repairs transaction expiry and absolute SDR start scheduling across PPS updates; see `artifacts/runtime-fixes/RESULTS.md`. The image includes the patch and manifest under `/usr/share/doc/containerlab-vrt/`. `scripts/build-image.sh` runs `scripts/verify_vrt_source.py`, checking the pinned include-tree hash in `container/vrt-source.json` and matching revision metadata before building. Direct Dockerfile invocation bypasses that host-side check; use the reviewed build script and retain image ID evidence.

## Historical source provenance caveat

Docker copies `third_party/vrt_framework` from the build context; `VRT_REVISION` is image-label metadata and does not select or verify that source. At the 2026-09-25 review, the checked-out submodule was `51853ba29703f51aceb2cfefe5a12a65a8e1110f`, while the manifest still named the historical `dbe85d3` baseline. Record `git submodule status` alongside the image ID; the existing label alone is not reliable source provenance. Align and enforce this metadata before the next image qualification.
