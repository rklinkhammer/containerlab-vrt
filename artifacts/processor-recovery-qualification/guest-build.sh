#!/bin/bash
set -euo pipefail
cd "$HOME/containerlab-vrt"
mkdir -p artifacts/processor-recovery-qualification
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
sudo docker image inspect containerlab-vrt-app:local > artifacts/processor-recovery-qualification/image.json

containerlab deploy --help > artifacts/processor-recovery-qualification/deploy-help.txt
containerlab destroy --help > artifacts/processor-recovery-qualification/destroy-help.txt
containerlab redeploy --help > artifacts/processor-recovery-qualification/redeploy-help.txt
containerlab version > artifacts/processor-recovery-qualification/native-version.txt
