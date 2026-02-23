// SPDX-License-Identifier: MIT
#include <arch/armv7/exception.hpp>
#include <arch/armv7/uart.hpp>
#include <kernel/print.hpp>
#include <lib/type_traits.hpp>
#include <stdint.h>

// Verification of type_traits
static_assert(lib::is_same_v<lib::int32_t, int>);
static_assert(lib::is_same_v<lib::uint32_t, unsigned int>);
static_assert(lib::is_same_v<lib::remove_reference_t<int&>, int>);
static_assert(lib::is_same_v<lib::remove_cv_t<const volatile int>, int>);

#include <kernel/boot_protocol.hpp>

extern "C" {

// UEFI entry point
void kernel_main(rucux_boot_info* info) {
    arch::armv7::uart::init();
    arch::armv7::exceptions_init();

    kernel::print("rucux (armv7) Initialized (UEFI)!\n");
    kernel::print("Magic: {}, Info: {}\n", reinterpret_cast<void*>(info ? info->magic : 0),
                  reinterpret_cast<void*>(info));

    // Halt the CPU. ARM doesn't have a direct 'hlt' like x86.
    // We use WFI (Wait For Interrupt).
    while (true) {
        asm volatile("wfi");
    }
}

} // extern "C"
