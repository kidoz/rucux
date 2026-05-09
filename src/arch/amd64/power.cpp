// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/power.hpp>

namespace arch::amd64::power {
namespace {

void halt_forever() noexcept {
    while (true) {
        asm volatile("hlt");
    }
}

void wait_for_kbc_input_clear() noexcept {
    for (int spin = 0; spin < 100000; ++spin) {
        if ((inb(0x64) & 0x02u) == 0) {
            return;
        }
        io_wait();
    }
}

} // namespace

void reboot() noexcept {
    asm volatile("cli");

    // Pulse the legacy keyboard controller reset line as a broad x86 fallback.
    wait_for_kbc_input_clear();
    outb(0x64, 0xfe);
    halt_forever();
}

void power_off() noexcept {
    asm volatile("cli");

    // Try the common QEMU/ACPI and Bochs-compatible shutdown ports.
    outw(0x604, 0x2000);
    outw(0xb004, 0x2000);
    outw(0x4004, 0x3400);
    halt_forever();
}

} // namespace arch::amd64::power

