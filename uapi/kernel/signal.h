// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__)
struct sigcontext {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx;
    uint64_t rsp, rip, rflags;
    uint64_t oldmask;
};
#elif defined(__arm__)
struct sigcontext {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11, r12, sp, lr, pc;
    uint32_t cpsr;
    uint32_t oldmask;
};
#endif

#ifdef __cplusplus
}
#endif
