// SPDX-License-Identifier: MIT
#include <arch/amd64/uart.hpp>
#include <kernel/print.hpp>

namespace kernel {

void kputc(char c) noexcept {
    arch::amd64::uart::putc(c);
}

void kwrite(const char* s) noexcept {
    arch::amd64::uart::write(s);
}

} // namespace kernel
