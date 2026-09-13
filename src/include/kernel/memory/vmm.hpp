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
    // Reclaim an unpublished address space whose user frames are privately
    // owned (e.g. a failed exec). It must not be active on any CPU.
    static void discard_address_space(uintptr_t root) noexcept;
    static void map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept;
    static void map_2mb(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept;
    static void unmap(uintptr_t virt) noexcept;
    static void switch_to(uintptr_t pml4_phys) noexcept;
    static uintptr_t get_active_page_table() noexcept;
    static uintptr_t get_phys(uintptr_t virt) noexcept;
    // Caller holds user_mapping_lock. Returns zero unless every paging level
    // permits EL0/user access, including write permission when requested.
    static uintptr_t get_user_phys(uintptr_t virt, bool write) noexcept;
    static void disable_write_protect() noexcept;
    static void enable_write_protect() noexcept;

    // Handle page fault for demand paging. Returns true if handled.
    static bool handle_page_fault(uintptr_t fault_addr, uint64_t error_code) noexcept;

#if defined(__aarch64__)
    // Program this CPU's translation registers from the tables the boot CPU
    // already built, then enable the MMU. Used by secondary cores, which start
    // with translation off and must join the existing address space rather than
    // construct a second one.
    static void enable_on_this_cpu() noexcept;
#endif
};

} // namespace kernel::memory
