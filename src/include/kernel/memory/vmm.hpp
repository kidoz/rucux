// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

enum class page_flags : uint64_t {
    NONE = 0,
    PRESENT = (1 << 0),
    WRITABLE = (1 << 1),
    USER = (1 << 2),
    NO_EXECUTE = (1ULL << 63)
};

inline page_flags operator|(page_flags a, page_flags b) {
    return static_cast<page_flags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

class vmm {
public:
    static void init() noexcept;
    static uintptr_t create_address_space() noexcept;
    static void map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept;
    static void unmap(uintptr_t virt) noexcept;
    static void switch_to(uintptr_t pml4_phys) noexcept;
};

} // namespace kernel::memory
