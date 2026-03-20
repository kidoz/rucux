// SPDX-License-Identifier: MIT
#pragma once

namespace arch::amd64::console {

void init_early() noexcept;
void enable_framebuffer() noexcept;

} // namespace arch::amd64::console
