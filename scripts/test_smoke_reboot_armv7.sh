#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

BOOT1_PRODUCT="smoke-reboot-qemu-armv7"
BOOT2_PRODUCT="smoke-qemu-armv7"
IMAGE="qemu-armv7-virt"
BOOT1_BUILD_DIR="builddir-arm-reboot-smoke"
BOOT2_BUILD_DIR="builddir-arm-smoke"
BOOT1_KERNEL_PATH="${BOOT1_BUILD_DIR}/kernel.elf"
BOOT2_KERNEL_PATH="${BOOT2_BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_ARM_REBOOT_SMOKE_TIMEOUT:-40}"
BOOT1_LOG="${RUCUX_ARM_REBOOT_SMOKE_BOOT1_LOG:-smoke_reboot_armv7_boot1.log}"
BOOT2_LOG="${RUCUX_ARM_REBOOT_SMOKE_BOOT2_LOG:-smoke_reboot_armv7_boot2.log}"

boot1_markers=(
    "rucux (armv7) Initialized!"
    "unixsock_smoke: PASS"
    "reboot_smoke: PASS armv7, requesting reboot"
    "power: reboot requested"
)

boot2_markers=(
    "rucux (armv7) Initialized!"
    "unixsock_smoke: PASS"
    "poweroff_smoke: PASS, requesting poweroff"
    "power: poweroff requested"
)

setup_builddir() {
    local build_dir="$1"
    local product="$2"

    if [ -d "${build_dir}" ]; then
        meson setup "${build_dir}" --reconfigure --cross-file cross_armv7.txt -Dproduct="${product}" >/dev/null
    else
        meson setup "${build_dir}" --cross-file cross_armv7.txt -Dproduct="${product}" >/dev/null
    fi
}

fail() {
    echo "smoke-reboot-armv7: $1" >&2
    echo "smoke-reboot-armv7: see ${BOOT1_LOG} and ${BOOT2_LOG}" >&2
    exit 1
}

run_boot() {
    local boot_index="$1"
    local product="$2"
    local kernel_path="$3"
    local log_path="$4"
    local elapsed=0

    SECONDS=0
    RUCUX_KERNEL_ARTIFACT="${kernel_path}" \
        scripts/run_qemu.sh --product "${product}" --image "${IMAGE}" --headless --timeout "${TIMEOUT_SECS}" >"${log_path}" 2>&1
    elapsed=$SECONDS

    if [ "${elapsed}" -ge "${TIMEOUT_SECS}" ]; then
        fail "boot ${boot_index} hit the timeout budget (${elapsed}s >= ${TIMEOUT_SECS}s)"
    fi
}

check_markers() {
    local log_path="$1"
    shift
    local marker

    for marker in "$@"; do
        if ! grep -Fq "${marker}" "${log_path}"; then
            fail "missing marker in ${log_path}: ${marker}"
        fi
    done
}

setup_builddir "${BOOT1_BUILD_DIR}" "${BOOT1_PRODUCT}"
meson compile -C "${BOOT1_BUILD_DIR}"

setup_builddir "${BOOT2_BUILD_DIR}" "${BOOT2_PRODUCT}"
meson compile -C "${BOOT2_BUILD_DIR}"

run_boot 1 "${BOOT1_PRODUCT}" "${BOOT1_KERNEL_PATH}" "${BOOT1_LOG}"
check_markers "${BOOT1_LOG}" "${boot1_markers[@]}"

run_boot 2 "${BOOT2_PRODUCT}" "${BOOT2_KERNEL_PATH}" "${BOOT2_LOG}"
check_markers "${BOOT2_LOG}" "${boot2_markers[@]}"

echo "smoke-reboot-armv7: PASS"
echo "smoke-reboot-armv7: logs captured in ${BOOT1_LOG} and ${BOOT2_LOG}"
