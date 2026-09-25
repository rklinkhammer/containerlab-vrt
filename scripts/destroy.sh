#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/runtime-common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/runtime-common.sh"
require_command docker
require_command containerlab

test -f "$TOPOLOGY" || {
  printf 'task topology is missing: %s\n' "$TOPOLOGY" >&2
  exit 2
}
test "$(id -u)" -eq 0 || {
  printf '%s\n' 'destroy requires root privileges for Containerlab networking' >&2
  exit 2
}

containerlab destroy --topo "$TOPOLOGY" --cleanup
residual=false
for container in "${CONTAINERS[@]}"; do
  if container_is_present "$container"; then
    printf 'residual task-owned container: %s\n' "$container" >&2
    residual=true
  fi
done
if docker network inspect "$MGMT_NETWORK" >/dev/null 2>&1; then
  printf 'residual task-owned network: %s\n' "$MGMT_NETWORK" >&2
  residual=true
fi
if [[ "$residual" = true ]]; then
  printf '%s\n' 'cleanup audit: FAIL; no unrelated resources were removed' >&2
  exit 4
fi
printf '%s\n' 'cleanup audit: PASS for the eight exact containers and management network'
