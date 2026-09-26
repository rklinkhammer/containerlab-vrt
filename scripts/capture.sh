#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=scripts/runtime-common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/runtime-common.sh"
require_command docker
verify_generated

require_command python3
exec python3 "$ROOT/scripts/capture_session.py"
