// SPDX-License-Identifier: MIT
#pragma once

namespace arch::aarch64::console {

// Registers the UART as the kernel console sink.
void init_early() noexcept;

} // namespace arch::aarch64::console
