# Third-party notices

## VRT framework

The project-owned source under `third_party/vrt_framework` derives from
`rklinkhammer/vrt_framework` revision
`51853ba29703f51aceb2cfefe5a12a65a8e1110f`, licensed under the MIT License.
Local changes are explicitly recorded in `patches/vrt-runtime-progress.patch`
and `patches/vrt-command-resumption.patch`; `container/vrt-source.json` pins
the patch hashes and resulting header tree.
Copyright (c) 2026 rklinkhammer.

The minimal P17 protocol vectors under `tests/golden` and their capture utility
under `tools` derive from historical revision
`dbe85d37155145842da60367af1c4beef8801b0c` and remain covered by that MIT
license and attribution.

## Four-radio behavioral reference

The virtual-tone and FFT behavior in `src/virtual_radio.cpp` and
`src/spectrum.cpp` was adapted from `rklinkhammer/graphx-docker` revision
`7cad4da8646eda302a005070228495c1aa87d89a`, licensed under the MIT License.
Copyright (c) 2026 GraphX contributors.

The full license texts are available at the pinned upstream revisions. No
GraphX runtime, executable, configuration loader, deployment service, image, or
library is included or required by this project.
