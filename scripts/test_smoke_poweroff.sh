#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-qemu-amd64"
BUILD_DIR="builddir-smoke"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_SMOKE_TIMEOUT:-40}"
LOG_PATH="${RUCUX_SMOKE_LOG:-smoke_poweroff_run.log}"

required_markers=(
    "unixsock_smoke: PASS"
    "dmesg_smoke: starting"
    "poweroff_smoke: PASS, requesting poweroff"
    "power: poweroff requested"
)

setup_builddir() {
    if [ -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --reconfigure --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    else
        meson setup "${BUILD_DIR}" --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    fi
}

fail() {
    echo "smoke-poweroff: $1" >&2
    echo "smoke-poweroff: see ${LOG_PATH}" >&2
    exit 1
}

setup_builddir
meson compile -C "${BUILD_DIR}"

SECONDS=0
RUCUX_KERNEL_ARTIFACT="${KERNEL_PATH}" \
    scripts/run_qemu.sh --product "${PRODUCT}" --headless --timeout "${TIMEOUT_SECS}" >"${LOG_PATH}" 2>&1
elapsed=$SECONDS

for marker in "${required_markers[@]}"; do
    if ! grep -Fq "${marker}" "${LOG_PATH}"; then
        fail "missing marker: ${marker}"
    fi
done

if [ "${elapsed}" -ge "${TIMEOUT_SECS}" ]; then
    fail "QEMU run hit the timeout budget (${elapsed}s >= ${TIMEOUT_SECS}s)"
fi

echo "smoke-poweroff: PASS (${elapsed}s)"
echo "smoke-poweroff: log captured in ${LOG_PATH}"
