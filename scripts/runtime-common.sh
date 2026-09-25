#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TOPOLOGY="$ROOT/generated/four-radio.clab.yml"
# These constants are consumed by scripts that source this file.
# shellcheck disable=SC2034
MGMT_NETWORK=four-radio-sdr-mgmt
# shellcheck disable=SC2034
CONTAINERS=(
  clab-four-radio-sdr-radio1
  clab-four-radio-sdr-radio2
  clab-four-radio-sdr-radio3
  clab-four-radio-sdr-radio4
  clab-four-radio-sdr-processor
  clab-four-radio-sdr-detector
  clab-four-radio-sdr-recorder
  clab-four-radio-sdr-switch1
)

require_command() {
  command -v "$1" >/dev/null 2>&1 || {
    printf 'required command is unavailable: %s\n' "$1" >&2
    exit 2
  }
}

verify_generated() {
  require_command python3
  python3 "$ROOT/scripts/generate_config.py" --check
  python3 - "$TOPOLOGY" <<'PY'
from pathlib import Path
import sys

text = Path(sys.argv[1]).read_text()
if not text.startswith("name: four-radio-sdr\nprefix: clab\n"):
    raise SystemExit("refusing topology with an unexpected lab name or prefix")
if "docker.sock" in text or "podman.sock" in text:
    raise SystemExit("refusing topology containing a privileged daemon socket")
PY
}

container_is_present() {
  docker container inspect "$1" >/dev/null 2>&1
}
