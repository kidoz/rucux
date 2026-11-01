// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

class vma_manager;
struct address_space;

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
    // Reclaim an inactive address space. With no areas, all user frames are
    // private (failed exec). Otherwise skip borrowed VMA frames; keep areas and
    // their backing-node references alive until the page-table walk completes.
    // The root must not be active on any CPU.
    static void discard_address_space(uintptr_t root, const vma_manager* areas = nullptr) noexcept;
    static void map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept;
    static void map_2mb(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept;
    static void unmap(uintptr_t virt) noexcept;
    // Replace permission bits of one present 4 KiB leaf and invalidate it.
    // The frame and non-permission bits stay. A miss or a large page returns false.
    static bool protect(uintptr_t virt, page_flags flags) noexcept;
    static void switch_to(uintptr_t pml4_phys, address_space* mm = nullptr) noexcept;
    static bool is_active(uintptr_t root) noexcept;
    // Lock-free NMI consumer on amd64; never acquires a mapping/allocator lock.
#ifdef __x86_64__
    static bool service_tlb_nmi() noexcept;
#ifdef RUCUX_TLB_SELF_TEST
    static bool verify_tlb_nmi() noexcept;
#endif
#ifdef RUCUX_AMD64_SMP_SELF_TEST
    static bool verify_remote_tlb() noexcept;
    static void probe_tlb_on_ap(uint32_t id) noexcept;
    static void report_tlb_probe() noexcept;
#endif
#endif
    static uintptr_t get_active_page_table() noexcept;
    static uintptr_t get_phys(uintptr_t virt) noexcept;
    // Caller holds user_mapping_lock. Returns zero unless every paging level
    // permits EL0/user access, including write permission when requested.
    static uintptr_t get_user_phys(uintptr_t virt, bool write) noexcept;
    static void disable_write_protect() noexcept;
    static void enable_write_protect() noexcept;

    // Handle page fault for demand paging. Returns true if handled.
    static bool handle_page_fault(uintptr_t fault_addr, uint64_t error_code) noexcept;

#if defined(__aarch64__) || defined(__arm__)
    // Program this CPU's translation registers from the tables the boot CPU
    // already built, then enable the MMU. Used by secondary cores, which start
    // with translation off and must join the existing address space rather than
    // construct a second one.
    static void enable_on_this_cpu() noexcept;
#endif
#ifdef __arm__
    static void configure_ram(uintptr_t base, size_t size) noexcept;
#endif
};

} // namespace kernel::memory
