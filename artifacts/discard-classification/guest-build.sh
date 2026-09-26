#!/bin/bash
set -euo pipefail
cd "$HOME/containerlab-vrt"
mkdir -p artifacts/discard-classification
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update
sudo apt-get install -y --no-install-recommends git python3
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$work"
 curl -fLO https://github.com/srl-labs/containerlab/releases/download/v0.79.0/checksums.txt
 curl -fLO https://github.com/srl-labs/containerlab/releases/download/v0.79.0/containerlab_0.79.0_linux_arm64.tar.gz
 grep ' containerlab_0.79.0_linux_arm64.tar.gz$' checksums.txt | sha256sum -c -
 tar -xzf containerlab_0.79.0_linux_arm64.tar.gz containerlab
 sudo install -m 0755 containerlab /usr/local/bin/containerlab)
# Bounded Docker log retention in this fresh, task-owned daemon only.
printf '%s\n' '{"log-driver":"json-file","log-opts":{"max-size":"8m","max-file":"3"}}' | sudo tee /etc/docker/daemon.json
sudo systemctl restart docker
containerlab version
sudo docker version
python3 scripts/generate_config.py
python3 scripts/verify_vrt_source.py
sudo bash scripts/build-image.sh
sudo docker image inspect containerlab-vrt-app:local > artifacts/discard-classification/image.json
# Same pinned build dependencies and sources, with tests and assertions enabled.
python3 - <<'PY'
from pathlib import Path
s=Path('Dockerfile').read_text().split('FROM ${BASE_IMAGE} AS runtime')[0]
s=s.replace('COPY src src','COPY src src\nCOPY tests tests').replace('-DBUILD_TESTING=OFF','-DBUILD_TESTING=ON').replace('-DCMAKE_BUILD_TYPE=Release','-DCMAKE_BUILD_TYPE=Debug')
Path('Dockerfile.qualification').write_text(s)
PY
source container/dependencies.env
sudo docker build -f Dockerfile.qualification -t containerlab-vrt-tests:qualification \
 --build-arg "BASE_IMAGE=$BASE_IMAGE" --build-arg "DEBIAN_SNAPSHOT=$DEBIAN_SNAPSHOT" \
 --build-arg "NLOHMANN_JSON_VERSION=$NLOHMANN_JSON_VERSION" \
 --build-arg "NLOHMANN_JSON_SHA256=$NLOHMANN_JSON_SHA256" .
sudo docker run --rm --network none -v "$PWD/generated:/generated:ro" containerlab-vrt-tests:qualification \
 ctest --test-dir /tmp/build --output-on-failure | tee artifacts/discard-classification/ctest.log
