// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
namespace arch::amd64 {
// FXSAVE area for the baseline x87/SSE ABI. AVX is not enabled by this kernel.
struct alignas(16) fpu_state {
    uint16_t control = 0x037f;
    uint8_t header[22]{};
    uint32_t mxcsr = 0x1f80;
    uint8_t remaining[484]{};
};
static_assert(sizeof(fpu_state) == 512);
inline void switch_fpu(fpu_state* old, const fpu_state& next) noexcept {
    if (old) asm volatile("fxsave64 %0" : "=m"(*old)::"memory");
    asm volatile("fxrstor64 %0" ::"m"(next) : "memory");
}
} // namespace arch::amd64
