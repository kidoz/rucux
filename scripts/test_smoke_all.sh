#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

run_gate() {
    local name="$1"
    local script_path="$2"

    echo "smoke-all: running ${name}"
    "${script_path}"
}

run_gate "amd64 poweroff" "scripts/test_smoke_poweroff.sh"
run_gate "amd64 reboot" "scripts/test_smoke_reboot.sh"
run_gate "armv7 poweroff" "scripts/test_smoke_armv7.sh"
run_gate "armv7 reboot" "scripts/test_smoke_reboot_armv7.sh"

echo "smoke-all: PASS"
