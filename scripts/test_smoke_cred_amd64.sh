#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-cred-qemu-amd64"
BUILD_DIR="builddir-cred-smoke"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_CRED_SMOKE_TIMEOUT:-25}"
LOG_PATH="${RUCUX_CRED_SMOKE_LOG:-smoke_cred_run.log}"

setup_builddir() {
    if [ -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --reconfigure --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    else
        meson setup "${BUILD_DIR}" --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    fi
}

fail() {
    echo "smoke-cred-amd64: $1" >&2
    echo "smoke-cred-amd64: see ${LOG_PATH}" >&2
    exit 1
}

setup_builddir
meson compile -C "${BUILD_DIR}"

RUCUX_KERNEL_ARTIFACT="${KERNEL_PATH}" \
    scripts/run_qemu.sh --product "${PRODUCT}" --headless --timeout "${TIMEOUT_SECS}" >"${LOG_PATH}" 2>&1

if ! grep -Fq "cred_smoke: PASS" "${LOG_PATH}"; then
    fail "missing marker: cred_smoke: PASS"
fi

if grep -Eiq "panic|page fault|exception|SPAWN: .*not found" "${LOG_PATH}"; then
    fail "fault marker found in QEMU log"
fi

echo "smoke-cred-amd64: PASS"
echo "smoke-cred-amd64: log captured in ${LOG_PATH}"
