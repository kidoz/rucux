// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

struct context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

} // namespace arch::amd64
