#!/bin/bash
set -e

cd "$(dirname "$0")/.."

if [ ! -d "builddir" ]; then
    echo "builddir not found. Please compile the kernel first."
    exit 1
fi

if [ ! -f "bootloader/BOOTX64.EFI" ]; then
    echo "Building UEFI Bootloader..."
    make -C bootloader ARCH=x86_64
fi

echo "Creating UEFI FAT32 Disk Image (uefi.img)..."
dd if=/dev/zero of=uefi.img bs=1M count=48 status=none
mformat -i uefi.img -F ::
mmd -i uefi.img ::/EFI
mmd -i uefi.img ::/EFI/BOOT

echo "Copying Bootloader and Kernel..."
mcopy -i uefi.img bootloader/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
mcopy -i uefi.img builddir/kernel.elf ::/kernel.elf

echo "Starting QEMU..."
if [ ! -s "vars.fd" ]; then
    cp /opt/homebrew/share/qemu/edk2-i386-vars.fd vars.fd || dd if=/dev/zero of=vars.fd bs=1K count=128
fi

TIMEOUT_CMD="timeout"
if command -v gtimeout &> /dev/null; then
    TIMEOUT_CMD="gtimeout"
fi

$TIMEOUT_CMD 10 qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd -drive if=pflash,format=raw,file=./vars.fd -drive file=uefi.img,if=ide,format=raw -serial file:uefi_serial.log -vga std -display none -no-reboot -m 256M || true
cat uefi_serial.log
