#!/bin/bash
set -euo pipefail
cd "$HOME/containerlab-vrt"
cat > Dockerfile.benchmark <<'DOCKER'
FROM containerlab-vrt-tests:qualification
RUN apt-get -o Acquire::Check-Valid-Until=false update && apt-get install -y --no-install-recommends python3 && rm -rf /var/lib/apt/lists/*
DOCKER
sudo docker build -f Dockerfile.benchmark -t containerlab-vrt-benchmark:qualification .
sudo docker run --rm --network none -v /tmp/overhead.py:/tmp/overhead.py:ro \
 containerlab-vrt-benchmark:qualification python3 /tmp/overhead.py > artifacts/linux-runtime-qualification/overhead.json
sudo docker run --rm --network none -e SDR_BIN=/tmp/build \
 -v "$PWD/scripts:/src/scripts:ro" -v "$PWD/generated:/src/generated:ro" \
 containerlab-vrt-benchmark:qualification python3 /src/scripts/test_telemetry_process.py \
 > artifacts/linux-runtime-qualification/process-tests.log
