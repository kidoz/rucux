// SPDX-License-Identifier: MIT
#include <arch/armv7/console.hpp>
#include <arch/armv7/uart.hpp>
#include <kernel/console.hpp>

namespace arch::armv7::console {
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

} // namespace arch::armv7::console
