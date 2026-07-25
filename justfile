# Run clang-format on all C/C++ files
format:
    ninja -C builddir clang-format

# Run clang-tidy on all C/C++ files
lint:
    ninja -C builddir clang-tidy

# Run both formatter and linter
check: format lint

# Reconfigure the build directory
reconfigure:
    meson setup --reconfigure builddir

# Build the kernel (amd64)
build:
    meson setup builddir --cross-file cross_amd64.txt 2>/dev/null || true
    meson compile -C builddir

# Run the kernel in QEMU with E1000 NIC
run: build
    #!/usr/bin/env bash
    set -e
    cd "{{justfile_directory()}}"

    # Build bootloader if needed
    if [ ! -f bootloader/BOOTX64.EFI ]; then
        echo "Building UEFI Bootloader..."
        make -C bootloader ARCH=x86_64
    fi

    # Create UEFI FAT32 disk image
    echo "Creating UEFI disk image..."
    dd if=/dev/zero of=uefi.img bs=1M count=48 status=none
    mformat -i uefi.img -F ::
    mmd -i uefi.img ::/EFI
    mmd -i uefi.img ::/EFI/BOOT
    mcopy -i uefi.img bootloader/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
    mcopy -i uefi.img builddir/kernel.elf ::/kernel.elf

    # Prepare OVMF vars
    if [ ! -s vars.fd ]; then
        cp /opt/homebrew/share/qemu/edk2-i386-vars.fd vars.fd 2>/dev/null || \
        dd if=/dev/zero of=vars.fd bs=1K count=128 status=none
    fi

    echo "Starting QEMU..."
    qemu-system-x86_64 \
        -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
        -drive if=pflash,format=raw,file=./vars.fd \
        -drive file=uefi.img,if=ide,format=raw \
        -device e1000,netdev=net0 \
        -netdev user,id=net0 \
        -serial stdio \
        -vga std \
        -no-reboot \
        -m 256M

# Run kernel in QEMU headless (serial output only, auto-exit after timeout)
run-headless timeout="15": build
    #!/usr/bin/env bash
    set -e
    cd "{{justfile_directory()}}"

    if [ ! -f bootloader/BOOTX64.EFI ]; then
        make -C bootloader ARCH=x86_64
    fi

    dd if=/dev/zero of=uefi.img bs=1M count=48 status=none
    mformat -i uefi.img -F ::
    mmd -i uefi.img ::/EFI
    mmd -i uefi.img ::/EFI/BOOT
    mcopy -i uefi.img bootloader/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
    mcopy -i uefi.img builddir/kernel.elf ::/kernel.elf

    if [ ! -s vars.fd ]; then
        cp /opt/homebrew/share/qemu/edk2-i386-vars.fd vars.fd 2>/dev/null || \
        dd if=/dev/zero of=vars.fd bs=1K count=128 status=none
    fi

    rm -f uefi_serial.log
    TIMEOUT_CMD="timeout"
    command -v gtimeout &>/dev/null && TIMEOUT_CMD="gtimeout"

    $TIMEOUT_CMD {{timeout}} qemu-system-x86_64 \
        -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
        -drive if=pflash,format=raw,file=./vars.fd \
        -drive file=uefi.img,if=ide,format=raw \
        -device e1000,netdev=net0 \
        -netdev user,id=net0 \
        -serial file:uefi_serial.log \
        -vga std \
        -display none \
        -no-reboot \
        -m 256M || true

    echo "=== Serial Output ==="
    cat uefi_serial.log

# Run all unit tests (host-compiled)
test:
    cd tests && make run

# Install Python tool dependencies into .venv (from uv.lock)
py-sync:
    uv sync --all-groups

# Run a project Python tool, e.g. `just py tools/product_info.py get dev-qemu-amd64 name`
py *ARGS:
    uv run python {{ARGS}}

# Clean build artifacts
clean:
    rm -rf builddir builddir-test uefi.img uefi_serial.log vars.fd
    cd tests && make clean 2>/dev/null || true
