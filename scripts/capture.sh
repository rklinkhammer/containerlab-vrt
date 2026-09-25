#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/runtime-common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/runtime-common.sh"
require_command docker
verify_generated

RECORDER=clab-four-radio-sdr-recorder
REMOTE_FILE=/tmp/containerlab-vrt-diagnostic.pcap
REMOTE_METRICS=/tmp/containerlab-vrt-diagnostic.json
OUTPUT_DIR="$ROOT/artifacts/runtime"
OUTPUT_FILE="$OUTPUT_DIR/four-radio-sdr-recorder.pcap"
OUTPUT_METRICS="$OUTPUT_DIR/four-radio-sdr-recorder-capture.json"

container_is_present "$RECORDER" || {
  printf 'required task-owned container is absent: %s\n' "$RECORDER" >&2
  exit 3
}
test "$(docker inspect --format '{{.State.Running}}' "$RECORDER")" = true || {
  printf 'required task-owned container is not running: %s\n' "$RECORDER" >&2
  exit 3
}

cleanup_remote() {
  docker exec "$RECORDER" rm -f "$REMOTE_FILE" "$REMOTE_METRICS" >/dev/null 2>&1 || true
}
trap cleanup_remote EXIT INT TERM
cleanup_remote
mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT_FILE" "$OUTPUT_METRICS"

capture_status=0
docker exec "$RECORDER" timeout --signal=INT --kill-after=5s 65s \
  recorder --interface eth1 --metrics "$REMOTE_METRICS" \
  --duration-seconds 60 --pcap "$REMOTE_FILE" \
  --capture-bytes 67108864 || capture_status=$?
if [[ "$capture_status" -ne 0 ]]; then
  printf 'bounded capture failed with status %s\n' "$capture_status" >&2
  exit "$capture_status"
fi
docker exec "$RECORDER" test -f "$REMOTE_FILE"
docker cp "$RECORDER:$REMOTE_FILE" "$OUTPUT_FILE"
docker cp "$RECORDER:$REMOTE_METRICS" "$OUTPUT_METRICS"
if ! size=$(stat -c '%s' "$OUTPUT_FILE" 2>/dev/null); then
  size=$(stat -f '%z' "$OUTPUT_FILE")
fi
if (( size > 67108864 )); then
  printf 'capture exceeded 64 MiB bound: %s bytes\n' "$size" >&2
  exit 4
fi
shasum -a 256 "$OUTPUT_FILE"
cat "$OUTPUT_METRICS"
printf 'capture_seconds_limit=60 capture_bytes=%s interface=eth1 namespace=%s selection=all-mirrored-frames\n' "$size" "$RECORDER"
