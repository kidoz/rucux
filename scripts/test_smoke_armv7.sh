#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-qemu-armv7"
IMAGE="qemu-armv7-virt"
BUILD_DIR="builddir-arm-smoke"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_ARM_SMOKE_TIMEOUT:-40}"
LOG_PATH="${RUCUX_ARM_SMOKE_LOG:-qemu_arm_smoke.log}"

required_markers=(
    "rucux (armv7) Initialized!"
    "rucux (armv7) boot complete, 1 CPUs online"
)

setup_builddir() {
    if [ -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --reconfigure --cross-file cross_armv7.txt -Dproduct="${PRODUCT}" >/dev/null
    else
        meson setup "${BUILD_DIR}" --cross-file cross_armv7.txt -Dproduct="${PRODUCT}" >/dev/null
    fi
}

fail() {
    echo "smoke-armv7: $1" >&2
    echo "smoke-armv7: see ${LOG_PATH}" >&2
    exit 1
}

setup_builddir
meson compile -C "${BUILD_DIR}"

KERNEL_BIN_PATH="${BUILD_DIR}/kernel.bin"
arm-none-eabi-objcopy -O binary "${KERNEL_PATH}" "${KERNEL_BIN_PATH}"

SECONDS=0
timeout "${TIMEOUT_SECS}" qemu-system-arm \
    -M virt -cpu cortex-a15 -m 256M \
    -kernel "${KERNEL_BIN_PATH}" \
    -serial stdio -display none -no-reboot -semihosting \
    >"${LOG_PATH}" 2>&1 || true
elapsed=$SECONDS

for marker in "${required_markers[@]}"; do
    if ! grep -Fq "${marker}" "${LOG_PATH}"; then
        fail "missing marker: ${marker}"
    fi
done

if [ "${elapsed}" -ge "${TIMEOUT_SECS}" ]; then
    echo "smoke-armv7: QEMU terminated by timeout (expected for kernel-only test)"
fi

echo "smoke-armv7: PASS (${elapsed}s)"
echo "smoke-armv7: log captured in ${LOG_PATH}"
