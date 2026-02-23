// SPDX-License-Identifier: MIT
#include <arch/armv7/uart.hpp>
#include <kernel/print.hpp>

namespace kernel {

void kputc(char c) noexcept {
    arch::armv7::uart::putc(c);
}

void kwrite(const char* s) noexcept {
    arch::armv7::uart::write(s);
}

} // namespace kernel
