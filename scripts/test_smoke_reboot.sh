#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-reboot-qemu-amd64"
BUILD_DIR="builddir-reboot-smoke"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_REBOOT_SMOKE_TIMEOUT:-40}"
BOOT1_LOG="${RUCUX_REBOOT_SMOKE_BOOT1_LOG:-smoke_reboot_boot1.log}"
BOOT2_LOG="${RUCUX_REBOOT_SMOKE_BOOT2_LOG:-smoke_reboot_boot2.log}"
JOURNAL_LOG="${RUCUX_REBOOT_SMOKE_JOURNAL_LOG:-smoke_reboot_journal.log}"
UEFI_IMAGE_PATH="$(python3 tools/product_info.py get-image "${PRODUCT}" qemu-dev board.artifacts.uefi_image)"

boot1_markers=(
    "unixsock_smoke: PASS"
    "dmesg_smoke: starting"
    "reboot_smoke: PASS boot=1, requesting reboot"
    "power: reboot requested"
)

boot2_markers=(
    "@rucux-journal boot=2 event=start"
    "unixsock_smoke: PASS"
    "dmesg_smoke: starting"
    "reboot_smoke: PASS boot=2, requesting poweroff"
    "power: poweroff requested"
)

journal_markers=(
    "@rucux-journal boot=1 event=start"
    "@rucux-journal boot=2 event=start"
)

setup_builddir() {
    if [ -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --reconfigure --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    else
        meson setup "${BUILD_DIR}" --cross-file cross_amd64.txt -Dproduct="${PRODUCT}" >/dev/null
    fi
}

fail() {
    echo "smoke-reboot: $1" >&2
    echo "smoke-reboot: see ${BOOT1_LOG}, ${BOOT2_LOG}, and ${JOURNAL_LOG}" >&2
    exit 1
}

run_boot() {
    local boot_index="$1"
    local log_path="$2"
    local skip_assemble="$3"
    local elapsed=0

    SECONDS=0
    RUCUX_KERNEL_ARTIFACT="${KERNEL_PATH}" \
    RUCUX_SKIP_ASSEMBLE="${skip_assemble}" \
        scripts/run_qemu.sh --product "${PRODUCT}" --headless --timeout "${TIMEOUT_SECS}" >"${log_path}" 2>&1
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

setup_builddir
meson compile -C "${BUILD_DIR}"

run_boot 1 "${BOOT1_LOG}" 0
check_markers "${BOOT1_LOG}" "${boot1_markers[@]}"

run_boot 2 "${BOOT2_LOG}" 1
check_markers "${BOOT2_LOG}" "${boot2_markers[@]}"

mtype -i "${UEFI_IMAGE_PATH}" ::/journal.log >"${JOURNAL_LOG}"
check_markers "${JOURNAL_LOG}" "${journal_markers[@]}"

echo "smoke-reboot: PASS"
echo "smoke-reboot: logs captured in ${BOOT1_LOG}, ${BOOT2_LOG}, and ${JOURNAL_LOG}"
