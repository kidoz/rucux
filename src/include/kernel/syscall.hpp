// SPDX-License-Identifier: MIT
#pragma once

namespace kernel {
using syscall_handler = long (*)(long, long, long, long, long, long, long);
// Marshals the public ABI to kernel-owned buffers; handlers never retain a
// user-memory pointer across a blocking operation.
long checked_syscall(syscall_handler handler, long number, long a1, long a2, long a3, long a4, long a5,
                     long a6) noexcept;
} // namespace kernel
