# Reproduce this bounded qualification

See docs/TELEMETRY.md for the application contract. Local checks:

```sh
python3 scripts/generate_config.py
python3 scripts/verify_vrt_source.py
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
python3 -m unittest discover -s tests -p 'test_generate_config.py'
python3 scripts/test_telemetry_process.py
python3 scripts/benchmark_telemetry.py
```

Configure a new macOS build when needed with `cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug`; pinned nlohmann/json3.12.0 and SoapySDR0.8.1 must be installed. The historical telemetry baseline had two independently recorded failures. They are repaired and the subsequent Linux qualification passes 15/15; see ../linux-runtime-qualification/RESULTS.md. Ordinary tests never create VMs.

Explicit Linux qualification: follow docs/MACOS.md to create a **new** dedicated VM from the pinned template. Do not reuse a stopped VM. Copy the current checkout into that new guest (no host mounts), including the vendored headers, config and generated files. In the guest source directory:

```sh
python3 scripts/generate_config.py --check
python3 scripts/verify_vrt_source.py
sudo bash scripts/build-image.sh
python3 scripts/qualify_telemetry_linux.py
sudo docker image inspect containerlab-vrt-app:local --format '{{.Id}}'
```

The explicit qualification script starts finite task-named containers on `--network none`, grants NET_RAW for the recorder checks, and writes reviewed loopback config derivatives under generated/ inside the disposable guest. It exercises radio/processor/detector idle heartbeat and recorder empty/full/bad-path cases; it is not the full switched SDR lab. Each container has bounded log rotation and `--rm`; a CID-file finally handler removes its exact created ID if interrupted by an exception/timeout. Originals remain unchanged. Preserve result files from prior attempts before rerunning.

Collect guest artifacts/telemetry/linux-results.json and the image ID, then stop the exact newly created VM from the Mac with `limactl stop "$VM_NAME"`. Keep a task-owned VM name record and clean up even after failure. Do not broadly prune containers or VMs. The recorded run created no Containerlab lab and published no services.

To qualify the full eight-node workflow later, use the existing reviewed deployment and destruction scripts in another fresh VM. Do not treat the isolated heartbeat tests as proof of coordinated streaming.
