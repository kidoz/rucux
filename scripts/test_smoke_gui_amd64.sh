#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-gui-qemu-amd64"
BUILD_DIR="builddir-gui-smoke"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_GUI_SMOKE_TIMEOUT:-25}"
LOG_PATH="${RUCUX_GUI_SMOKE_LOG:-smoke_gui_run.log}"

setup_builddir() {
    if [ -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --reconfigure --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    else
        meson setup "${BUILD_DIR}" --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    fi
}

fail() {
    echo "smoke-gui-amd64: $1" >&2
    echo "smoke-gui-amd64: see ${LOG_PATH}" >&2
    exit 1
}

setup_builddir
meson compile -C "${BUILD_DIR}"

RUCUX_KERNEL_ARTIFACT="${KERNEL_PATH}" \
    scripts/run_qemu.sh --product "${PRODUCT}" --headless --timeout "${TIMEOUT_SECS}" >"${LOG_PATH}" 2>&1

if ! grep -Fq "gui_smoke: presented" "${LOG_PATH}"; then
    fail "missing marker: gui_smoke: presented"
fi

if grep -Eiq "panic|page fault|exception|SPAWN: .*not found" "${LOG_PATH}"; then
    fail "fault marker found in QEMU log"
fi

echo "smoke-gui-amd64: PASS"
echo "smoke-gui-amd64: log captured in ${LOG_PATH}"
