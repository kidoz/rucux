// SPDX-License-Identifier: MIT
#include <arch/aarch64/console.hpp>
#include <arch/aarch64/uart.hpp>
#include <kernel/console.hpp>

namespace arch::aarch64::console {
namespace {

void uart_sink_putc(char c) noexcept {
    uart::putc(c);
}

constexpr kernel::console::sink UART_SINK = {
    uart_sink_putc,
    nullptr,
};

} // namespace

void init_early() noexcept {
    kernel::console::reset();
    kernel::console::register_sink(UART_SINK);
}

} // namespace arch::aarch64::console
