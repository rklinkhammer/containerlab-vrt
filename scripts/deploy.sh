#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/runtime-common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/runtime-common.sh"
require_command docker
require_command containerlab
verify_generated

test "$(id -u)" -eq 0 || {
  printf '%s\n' 'deploy requires root privileges for Containerlab networking' >&2
  exit 2
}

docker image inspect containerlab-vrt-app:local >/dev/null
for container in "${CONTAINERS[@]}"; do
  if container_is_present "$container"; then
    printf 'refusing to replace existing task-owned container: %s\n' "$container" >&2
    exit 3
  fi
done
if docker network inspect "$MGMT_NETWORK" >/dev/null 2>&1; then
  printf 'refusing to replace existing task-owned network: %s\n' "$MGMT_NETWORK" >&2
  exit 3
fi

completed=false
cleanup_partial() {
  if [[ "$completed" != true ]]; then
    printf '%s\n' 'deployment did not complete; removing only resources named by the generated topology' >&2
    containerlab destroy --topo "$TOPOLOGY" --cleanup || true
  fi
}
trap cleanup_partial EXIT INT TERM
containerlab deploy --topo "$TOPOLOGY"
completed=true
trap - EXIT INT TERM
printf '%s\n' 'deployment command completed; runtime qualification gates remain NOT_RUN'
