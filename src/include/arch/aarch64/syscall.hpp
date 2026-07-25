// SPDX-License-Identifier: MIT
#pragma once

namespace arch::aarch64 {

// Dispatch a syscall by number. Arguments follow the Linux/AArch64 convention:
// number in x8, arguments in x0-x5.
long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) noexcept;

// No-op on AArch64; SVC is routed by the exception vector table.
void syscall_init() noexcept;

} // namespace arch::aarch64
