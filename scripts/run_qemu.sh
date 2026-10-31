#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

cd "$(dirname "$0")/.."

PRODUCT="dev-qemu-amd64"
IMAGE=""
HEADLESS=0
TIMEOUT_SECS=10

while [ "$#" -gt 0 ]; do
    case "$1" in
        --product)
            PRODUCT="$2"
            shift 2
            ;;
        --image)
            IMAGE="$2"
            shift 2
            ;;
        --headless)
            HEADLESS=1
            shift
            ;;
        --timeout)
            TIMEOUT_SECS="$2"
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

BUILD_DIR="${RUCUX_BUILD_DIR:-build/${PRODUCT}}"
KERNEL_PATH="${RUCUX_KERNEL_ARTIFACT:-${BUILD_DIR}/kernel.elf}"
ARCH="$(python3 tools/product_info.py get "${PRODUCT}" arch)"

if [ ! -f "${KERNEL_PATH}" ]; then
    echo "kernel artifact not found: ${KERNEL_PATH}" >&2
    exit 1
fi

run_with_timeout() {
    local status=0
    local timeout_cmd=""

    if [ "${HEADLESS}" -eq 0 ]; then
        "$@"
        return
    fi

    if command -v timeout >/dev/null 2>&1; then
        timeout_cmd="timeout"
    elif command -v gtimeout >/dev/null 2>&1; then
        timeout_cmd="gtimeout"
    else
        echo "timeout or gtimeout is required for headless runs" >&2
        exit 1
    fi

    "${timeout_cmd}" "${TIMEOUT_SECS}" "$@" || status=$?
    if [ "${status}" -ne 0 ] && [ "${status}" -ne 124 ]; then
        exit "${status}"
    fi
}

case "${ARCH}" in
    amd64)
        IMAGE="${IMAGE:-uefi-disk}"
        if [ ! -f bootloader/BOOTX64.EFI ]; then
            make -C bootloader ARCH=x86_64
        fi

        if [ "${RUCUX_SKIP_ASSEMBLE:-0}" != "1" ]; then
            python3 tools/assemble_image.py "${PRODUCT}" "${IMAGE}" --build-dir "${BUILD_DIR}"
        fi

        IMAGE_PATH="build/images/${PRODUCT}/uefi.img"
        VARS_PATH="build/images/${PRODUCT}/uefi-vars.fd"
        UEFI_CODE="${RUCUX_UEFI_CODE:-/opt/homebrew/share/qemu/edk2-x86_64-code.fd}"
        UEFI_VARS_TEMPLATE="${RUCUX_UEFI_VARS_TEMPLATE:-/opt/homebrew/share/qemu/edk2-i386-vars.fd}"
        mkdir -p "$(dirname "${VARS_PATH}")"
        if [ ! -s "${VARS_PATH}" ]; then
            cp "${UEFI_VARS_TEMPLATE}" "${VARS_PATH}" 2>/dev/null || dd if=/dev/zero of="${VARS_PATH}" bs=1K count=128 status=none
        fi

        command=(
            qemu-system-x86_64
            -drive "if=pflash,format=raw,readonly=on,file=${UEFI_CODE}"
            -drive "if=pflash,format=raw,file=${VARS_PATH}"
            -drive "file=${IMAGE_PATH},if=ide,format=raw"
            -device e1000,netdev=net0
            -netdev user,id=net0
            -serial stdio
            -vga std
            -no-reboot
            -m "${RUCUX_QEMU_MEMORY:-256M}"
        )
        ;;
    armv7|aarch64)
        if [ "${ARCH}" = "armv7" ]; then
            emulator="qemu-system-arm"
            cpu="cortex-a15"
            KERNEL_BIN_PATH="${BUILD_DIR}/kernel.bin"
            arm-none-eabi-objcopy -O binary "${KERNEL_PATH}" "${KERNEL_BIN_PATH}"
            KERNEL_PATH="${KERNEL_BIN_PATH}"
        else
            emulator="qemu-system-aarch64"
            cpu="cortex-a53"
        fi
        command=(
            "${emulator}"
            -M virt
            -cpu "${cpu}"
            -kernel "${KERNEL_PATH}"
            -serial stdio
            -display none
            -monitor none
            -nic none
            -no-reboot
            -semihosting
            -m "${RUCUX_QEMU_MEMORY:-512M}"
        )
        ;;
    *)
        echo "unsupported product architecture: ${ARCH}" >&2
        exit 1
        ;;
esac

if [ "${HEADLESS}" -eq 1 ]; then
    command+=(-display none -monitor none)
fi

run_with_timeout "${command[@]}"
