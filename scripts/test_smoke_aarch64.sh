#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="smoke-qemu-aarch64"
BUILD_DIR="${RUCUX_AARCH64_BUILD_DIR:-builddir-aarch64-smoke}"
KERNEL_PATH="${BUILD_DIR}/kernel.elf"
TIMEOUT_SECS="${RUCUX_AARCH64_SMOKE_TIMEOUT:-40}"
LOG_PATH="${RUCUX_AARCH64_SMOKE_LOG:-qemu_aarch64_smoke.log}"
SMP_LOG_PATH="${RUCUX_AARCH64_SMP_LOG:-qemu_aarch64_smp.log}"

# Asserted from the 4-core run. "3 of 3" proves PSCI CPU_ON succeeded for every
# secondary, and an intact marker line proves console output is serialized —
# without the console lock these interleave and the line is unmatchable.
smp_markers=(
    "SMP: 3 of 3 secondary cores online"
    "rucux (aarch64) boot complete, 4 CPUs online"
    "AP1: timer IRQ verified"
    "AP2: timer IRQ verified"
    "AP3: timer IRQ verified"
    "userspace: scheduling verified"
    "syscall safety: PASS"
)

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
    "Scheduler: preemption reached a scheduled thread"
    "userspace: entering EL0"
    "userspace: hello from EL0"
    "userspace: scheduling verified"
    "syscall safety: PASS"
)

# A translation mismatch still boots, so assert its absence explicitly rather
# than relying on the positive markers alone.
forbidden_markers=(
    "syscall safety: FAIL"
    "*** EL0"
    "ELF load failed"
    "TRANSLATION MISMATCH"
    "CONTEXT SWITCH FAILED"
    "AArch64 synchronous fault"
    "AArch64 IRQ fault"
    "userspace: short write"
    "open_stdio: /dev/tty not found"
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

run_qemu() { # $1 = -smp value, $2 = log path
    local rc=0
    timeout "${TIMEOUT_SECS}" qemu-system-aarch64 \
        -M virt -cpu cortex-a53 -smp "$1" -m 512M \
        -kernel "${KERNEL_PATH}" \
        -serial stdio -display none -monitor none -nic none -no-reboot \
        >"$2" 2>&1 || rc=$?
    if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        fail "QEMU exited with status $rc (see $2)"
    fi
}

SECONDS=0

# Single core: userspace switching and syscall boundary checks.
run_qemu 1 "${LOG_PATH}"

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

# Four cores: secondary bring-up via PSCI, and serialized console output.
run_qemu 4 "${SMP_LOG_PATH}"

for marker in "${smp_markers[@]}"; do
    if ! grep -Fq "${marker}" "${SMP_LOG_PATH}"; then
        fail "missing SMP marker: ${marker} (see ${SMP_LOG_PATH})"
    fi
done

for marker in "${forbidden_markers[@]}"; do
    if grep -Fq "${marker}" "${SMP_LOG_PATH}"; then
        fail "unexpected SMP marker: ${marker} (see ${SMP_LOG_PATH})"
    fi
done

elapsed=$SECONDS

if [ "${elapsed}" -ge "${TIMEOUT_SECS}" ]; then
    echo "smoke-aarch64: QEMU terminated by timeout (expected after userspace tests finish)"
fi

echo "smoke-aarch64: PASS (${elapsed}s)"
echo "smoke-aarch64: log captured in ${LOG_PATH}"
