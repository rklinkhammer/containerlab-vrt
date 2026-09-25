#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
# shellcheck source=/dev/null
source "$ROOT/container/dependencies.env"
IMAGE=containerlab-vrt-app:local

command -v docker >/dev/null 2>&1 || {
  printf '%s\n' 'required command is unavailable: docker' >&2
  exit 2
}
python3 "$ROOT/scripts/generate_config.py" --check
python3 "$ROOT/scripts/verify_vrt_source.py"

docker build --pull \
  --file "$ROOT/Dockerfile" \
  --tag "$IMAGE" \
  --build-arg "BASE_IMAGE=$BASE_IMAGE" \
  --build-arg "DEBIAN_SNAPSHOT=$DEBIAN_SNAPSHOT" \
  --build-arg "NLOHMANN_JSON_VERSION=$NLOHMANN_JSON_VERSION" \
  --build-arg "NLOHMANN_JSON_SHA256=$NLOHMANN_JSON_SHA256" \
  --build-arg "VRT_REVISION=$VRT_REVISION" \
  --build-arg "GRAPHX_REFERENCE_REVISION=$GRAPHX_REFERENCE_REVISION" \
  "$ROOT"

docker run --rm --network none --entrypoint /bin/sh "$IMAGE" -eu -c '
  for command in radio processor detector recorder; do
    command -v "$command" >/dev/null
  done
  test ! -e /var/run/docker.sock
  test ! -e /run/docker.sock
'
printf 'built and inspected %s\n' "$IMAGE"
