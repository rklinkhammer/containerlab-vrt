# Container dependency manifest

| Input | Pin | Purpose |
| --- | --- | --- |
| Debian | `bookworm-20250908-slim@sha256:df52e55e3361a81ac1bead266f3373ee55d29aa50cf0975d440c2be3483d8ed3` | Build and runtime base; Docker Hub manifest last updated 2025-09-08 |
| Debian archive | `20250908T000000Z` | Immutable package repository snapshot |
| Debian security archive | `20250908T000000Z` | Immutable security package repository snapshot |
| nlohmann/json | `3.12.0`, SHA-256 `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa` | JSON parsing and metrics |
| VRT framework | `dbe85d37155145842da60367af1c4beef8801b0c` | Project-owned migrated SDR-profile source baseline, MIT |
| graphx-docker | `7cad4da8646eda302a005070228495c1aa87d89a` | Behavioral reference only, MIT; absent from build context and image |
| Nokia SR Linux | `25.10.1@sha256:bc8112667b5a87bee5039ade65b504ac2ef35511210d0675db6c7b0754e8cc4c` | Switch image, generated from `config/lab.json` |

The builder installs `ca-certificates`, `cmake`, `curl`, `g++`, `libsoapysdr-dev`, and `ninja-build` from the dated Debian snapshot. The runtime installs only `iproute2` and `libsoapysdr0.8` from the same snapshot. Snapshot pinning fixes transitive package resolution; `container/dependencies.env` is the machine-readable source for build arguments.

The application image itself is local and therefore has no repository digest before building. Record `docker image inspect containerlab-vrt-app:local --format '{{json .RepoDigests}} {{.Id}}'` with qualification evidence. A local image ID is build evidence, not a portable registry digest.

## Current source provenance caveat

Docker copies `third_party/vrt_framework` from the build context; `VRT_REVISION` is image-label metadata and does not select or verify that source. At the 2026-09-25 review, the checked-out submodule was `51853ba29703f51aceb2cfefe5a12a65a8e1110f`, while the manifest still named the historical `dbe85d3` baseline. Record `git submodule status` alongside the image ID; the existing label alone is not reliable source provenance. Align and enforce this metadata before the next image qualification.
