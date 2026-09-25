#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/runtime-common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/runtime-common.sh"
require_command docker
require_command containerlab
verify_generated

for container in "${CONTAINERS[@]}"; do
  container_is_present "$container" || {
    printf 'required task-owned container is absent: %s\n' "$container" >&2
    exit 3
  }
done

containerlab inspect --topo "$TOPOLOGY" --format json
for container in "${CONTAINERS[@]}"; do
  docker inspect --format \
    '{"name":"{{.Name}}","status":"{{.State.Status}}","running":{{.State.Running}},"restart_count":{{.RestartCount}},"oom_killed":{{.State.OOMKilled}}}' \
    "$container"
done

status_ports=(18701 18702 18703 18704)
for index in 0 1 2 3; do
  container="${CONTAINERS[$index]}"
  address=$(docker inspect --format \
    "{{(index .NetworkSettings.Networks \"$MGMT_NETWORK\").IPAddress}}" \
    "$container")
  if [[ -z "$address" ]]; then
    printf 'status unavailable: %s has no address on %s\n' "$container" "$MGMT_NETWORK" >&2
    continue
  fi
  python3 "$ROOT/scripts/status_probe.py" --host "$address" --port "${status_ports[$index]}" ||
    printf 'status unavailable: %s\n' "$container" >&2
done

for container in "${CONTAINERS[@]:0:7}"; do
  docker logs --tail 40 "$container" 2>&1 || true
done
docker exec clab-four-radio-sdr-recorder \
  cat /run/containerlab-vrt/recorder.json ||
  printf '%s\n' 'recorder metrics unavailable' >&2

switch_state=0
for index in 1 2 3 4 5 6 7; do
  docker exec clab-four-radio-sdr-switch1 \
    sr_cli -d -- info from state / interface "ethernet-1/$index" || switch_state=$?
done
docker exec clab-four-radio-sdr-switch1 \
  sr_cli -d -- info from state / network-instance app || switch_state=$?
docker exec clab-four-radio-sdr-switch1 \
  sr_cli -d -- info from state / system mirroring || switch_state=$?
if (( switch_state != 0 )); then
  printf '%s\n' 'switch operational state/counter gate: NOT_RUN (SR Linux command unavailable or rejected)' >&2
fi
printf '%s\n' 'inspection collected current state only; consult recorded qualification evidence for sustained, forwarding, mirroring, loss, and timing gates'
