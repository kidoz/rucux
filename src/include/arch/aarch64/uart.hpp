// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::aarch64 {

class uart {
public:
    // Falls back to the QEMU `virt` PL011 when no FDT-derived base is set.
    static void init() noexcept;
    static void init_dynamic(uintptr_t base, bool is_pl011) noexcept;
    static void putc(char c) noexcept;
    static void write(const char* s) noexcept;
};

} // namespace arch::aarch64
