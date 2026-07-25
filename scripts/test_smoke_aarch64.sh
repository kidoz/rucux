#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-qemu-aarch64"
BUILD_DIR="builddir-aarch64-smoke"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_AARCH64_SMOKE_TIMEOUT:-40}"
LOG_PATH="${RUCUX_AARCH64_SMOKE_LOG:-qemu_aarch64_smoke.log}"

# "boot complete" is only reached after the timer IRQ has been serviced five
# times, so it proves the GIC and generic timer are live — not merely programmed.
required_markers=(
    "rucux (aarch64) Initialized!"
    "Running at EL1"
    "Per-CPU: BSP (cpu_id=0) initialized, TPIDR_EL1 set"
    "AArch64 VMM: MMU enabled"
    "MMU: identity translation verified"
    "Scheduler initialized"
    "Scheduler: context switch round-trip verified"
    "rucux (aarch64) boot complete, 1 CPUs online"
)

# A translation mismatch still boots, so assert its absence explicitly rather
# than relying on the positive markers alone.
forbidden_markers=(
    "TRANSLATION MISMATCH"
)

setup_builddir() {
    if [ -d "${BUILD_DIR}" ]; then
        meson setup "${BUILD_DIR}" --reconfigure --cross-file cross_aarch64.txt -Dproduct="${PRODUCT}" >/dev/null
    else
        meson setup "${BUILD_DIR}" --cross-file cross_aarch64.txt -Dproduct="${PRODUCT}" >/dev/null
    fi
}

fail() {
    echo "smoke-aarch64: $1" >&2
    echo "smoke-aarch64: see ${LOG_PATH}" >&2
    exit 1
}

setup_builddir
meson compile -C "${BUILD_DIR}"

SECONDS=0
timeout "${TIMEOUT_SECS}" qemu-system-aarch64 \
    -M virt -cpu cortex-a53 -m 512M \
    -kernel "${KERNEL_PATH}" \
    -serial stdio -display none -no-reboot \
    >"${LOG_PATH}" 2>&1 || true
elapsed=$SECONDS

for marker in "${required_markers[@]}"; do
    if ! grep -Fq "${marker}" "${LOG_PATH}"; then
        fail "missing marker: ${marker}"
    fi
done

for marker in "${forbidden_markers[@]}"; do
    if grep -Fq "${marker}" "${LOG_PATH}"; then
        fail "unexpected marker: ${marker}"
    fi
done

if [ "${elapsed}" -ge "${TIMEOUT_SECS}" ]; then
    echo "smoke-aarch64: QEMU terminated by timeout (expected for kernel-only test)"
fi

echo "smoke-aarch64: PASS (${elapsed}s)"
echo "smoke-aarch64: log captured in ${LOG_PATH}"
