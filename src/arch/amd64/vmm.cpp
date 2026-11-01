// SPDX-License-Identifier: MIT
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vma.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

static uintptr_t* g_pml4;

// Page size bit (bit 7) — marks a 2MB page in a Page Directory entry
static constexpr uint64_t PAGE_SIZE_BIT = (1ULL << 7);

static uintptr_t* get_next_table(uintptr_t* table, size_t index, bool create) noexcept {
    if (table[index] & static_cast<uint64_t>(page_flags::PRESENT)) {
        return reinterpret_cast<uintptr_t*>(table[index] & 0x000FFFFFFFFFF000ULL);
    }

    if (!create) return nullptr;

    void* new_table = pmm::alloc_page();
    if (!new_table) return nullptr;

    lib::memset(new_table, 0, pmm::PAGE_SIZE);
    table[index] = reinterpret_cast<uintptr_t>(new_table) | static_cast<uint64_t>(page_flags::PRESENT) |
                   static_cast<uint64_t>(page_flags::WRITABLE) | static_cast<uint64_t>(page_flags::USER);
    return reinterpret_cast<uintptr_t*>(new_table);
}

void vmm::init() noexcept {
    // NX PTEs are reserved until EFER.NXE is enabled.
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
    lo |= 1U << 11;
    asm volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0xC0000080) : "memory");
    void* pml4 = pmm::alloc_page();
    lib::memset(pml4, 0, pmm::PAGE_SIZE);
    g_pml4 = static_cast<uintptr_t*>(pml4);

    uintptr_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    g_pml4[0] = reinterpret_cast<uintptr_t*>(current_cr3)[0];

    vmm::switch_to(reinterpret_cast<uintptr_t>(g_pml4));
}

uintptr_t vmm::create_address_space() noexcept {
    uintptr_t* new_pml4 = static_cast<uintptr_t*>(pmm::alloc_page());
    if (!new_pml4) return 0;
    lib::memset(new_pml4, 0, pmm::PAGE_SIZE);
    new_pml4[0] = g_pml4[0];
    return reinterpret_cast<uintptr_t>(new_pml4);
}

// The caller guarantees an inactive root. Drop page tables while retaining
// borrowed-frame owners; VMA backing references are released after this walk.
static void discard_private_table(uintptr_t address, unsigned depth, uintptr_t base,
                                  const vma_manager* areas) noexcept {
    auto* table = reinterpret_cast<uint64_t*>(address);
    for (size_t i = 0; i < 512; ++i) {
        if (!(table[i] & 1)) continue;
        uintptr_t child = static_cast<uintptr_t>(table[i] & 0x000FFFFFFFFFF000ULL);
        const uintptr_t virt = base + (static_cast<uintptr_t>(i) << (12 + 9 * (depth - 1)));
        if (depth == 1) {
            if (!areas || areas->owns_frame(virt)) pmm::free_page(reinterpret_cast<void*>(child));
        } else {
            discard_private_table(child, depth - 1, virt, areas);
        }
    }
    pmm::free_page(reinterpret_cast<void*>(address));
}

void vmm::discard_address_space(uintptr_t root, const vma_manager* areas) noexcept {
    if (!root || root == get_active_page_table()) return;
    irq_lock_guard guard(user_mapping_lock);
    auto* table = reinterpret_cast<uint64_t*>(root);
    for (size_t i = 1; i < 256; ++i) {
        if (table[i] & 1)
            discard_private_table(static_cast<uintptr_t>(table[i] & 0x000FFFFFFFFFF000ULL), 3,
                                  static_cast<uintptr_t>(i) << 39, areas);
    }
    pmm::free_page(reinterpret_cast<void*>(root));
}

void vmm::map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    uintptr_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uintptr_t* pml4 = reinterpret_cast<uintptr_t*>(current_cr3 & ~0xFFFULL);

    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uintptr_t* pdpt = get_next_table(pml4, pml4_idx, true);
    if (!pdpt) return;
    uintptr_t* pd = get_next_table(pdpt, pdpt_idx, true);
    if (!pd) return;
    uintptr_t* pt = get_next_table(pd, pd_idx, true);
    if (!pt) return;

    pt[pt_idx] = (phys & ~0xFFFULL) | static_cast<uint64_t>(flags);
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm::map_2mb(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    // 2MB pages use the Page Directory entry directly (no PT level).
    // virt and phys must be 2MB-aligned.
    uintptr_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uintptr_t* pml4 = reinterpret_cast<uintptr_t*>(current_cr3 & ~0xFFFULL);

    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;

    uintptr_t* pdpt = get_next_table(pml4, pml4_idx, true);
    if (!pdpt) return;
    uintptr_t* pd = get_next_table(pdpt, pdpt_idx, true);
    if (!pd) return;

    // Set the PS (Page Size) bit to indicate this is a 2MB page
    pd[pd_idx] = (phys & ~0x1FFFFFULL) | static_cast<uint64_t>(flags) | PAGE_SIZE_BIT;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm::unmap(uintptr_t virt) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uintptr_t* pdpt = get_next_table(reinterpret_cast<uintptr_t*>(get_active_page_table()), pml4_idx, false);
    if (!pdpt) return;
    uintptr_t* pd = get_next_table(pdpt, pdpt_idx, false);
    if (!pd) return;

    // Check if this is a 2MB page
    if (pd[pd_idx] & PAGE_SIZE_BIT) {
        pd[pd_idx] = 0;
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
        return;
    }

    uintptr_t* pt = get_next_table(pd, pd_idx, false);
    if (!pt) return;

    pt[pt_idx] = 0;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

bool vmm::protect(uintptr_t virt, page_flags flags) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    const size_t pml4_idx = (virt >> 39) & 0x1FF;
    const size_t pdpt_idx = (virt >> 30) & 0x1FF;
    const size_t pd_idx = (virt >> 21) & 0x1FF;
    const size_t pt_idx = (virt >> 12) & 0x1FF;

    auto* pml4 = reinterpret_cast<uintptr_t*>(get_active_page_table() & ~0xFFFULL);
    uintptr_t* pdpt = get_next_table(pml4, pml4_idx, false);
    if (!pdpt) return false;
    uintptr_t* pd = get_next_table(pdpt, pdpt_idx, false);
    if (!pd || (pd[pd_idx] & PAGE_SIZE_BIT)) return false;
    uintptr_t* pt = get_next_table(pd, pd_idx, false);
    if (!pt) return false;

    uint64_t entry = pt[pt_idx];
    if ((entry & static_cast<uint64_t>(page_flags::PRESENT)) == 0) return false;

    constexpr uint64_t permission_mask =
        static_cast<uint64_t>(page_flags::PRESENT) | static_cast<uint64_t>(page_flags::WRITABLE) |
        static_cast<uint64_t>(page_flags::USER) | static_cast<uint64_t>(page_flags::NO_EXECUTE);
    pt[pt_idx] = (entry & ~permission_mask) | (static_cast<uint64_t>(flags) & permission_mask);
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

void vmm::switch_to(uintptr_t pml4_phys) noexcept {
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uintptr_t vmm::get_active_page_table() noexcept {
    uintptr_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    return current_cr3;
}

uintptr_t vmm::get_phys(uintptr_t virt) noexcept {
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uintptr_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uintptr_t* pml4 = reinterpret_cast<uintptr_t*>(current_cr3 & ~0xFFFULL);

    uintptr_t* pdpt = get_next_table(pml4, pml4_idx, false);
    if (!pdpt) return 0;
    uintptr_t* pd = get_next_table(pdpt, pdpt_idx, false);
    if (!pd) return 0;

    // Check if this is a 2MB page
    if (pd[pd_idx] & PAGE_SIZE_BIT) {
        return (pd[pd_idx] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
    }

    uintptr_t* pt = get_next_table(pd, pd_idx, false);
    if (!pt) return 0;

    if (pt[pt_idx] & static_cast<uint64_t>(page_flags::PRESENT)) {
        return (pt[pt_idx] & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFF);
    }
    return 0;
}

uintptr_t vmm::get_user_phys(uintptr_t virt, bool write) noexcept {
    if (!user_range(virt, 1)) return 0;
    auto* table = reinterpret_cast<const uint64_t*>(get_active_page_table() & 0x000FFFFFFFFFF000ULL);
    const uint64_t required = 5 | (write ? 2 : 0);
    for (int shift = 39; shift >= 12; shift -= 9) {
        uint64_t entry = table[(virt >> shift) & 511];
        if ((entry & required) != required) return 0;
        if (shift == 12 || (shift != 39 && (entry & 128))) {
            const uint64_t mask = (1ULL << shift) - 1;
            return (entry & 0x000FFFFFFFFFF000ULL & ~mask) | (virt & mask);
        }
        table = reinterpret_cast<const uint64_t*>(entry & 0x000FFFFFFFFFF000ULL);
    }
    return 0;
}

void vmm::disable_write_protect() noexcept {
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %0, %%cr0" : : "r"(cr0 & ~(1ULL << 16)));
}

void vmm::enable_write_protect() noexcept {
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %0, %%cr0" : : "r"(cr0 | (1ULL << 16)));
}

bool vmm::handle_page_fault(uintptr_t fault_addr, uint64_t error_code) noexcept {
    // error_code bit 0: 0 = not present, 1 = protection violation
    if (error_code & 1) return false;
    return mmap_manager::handle_fault(fault_addr);
}

} // namespace kernel::memory
