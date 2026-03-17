// SPDX-License-Identifier: MIT
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

static uintptr_t* g_pml4;

static uintptr_t* get_next_table(uintptr_t* table, size_t index, bool create) noexcept {
    if (table[index] & static_cast<uint64_t>(page_flags::PRESENT)) {
        return reinterpret_cast<uintptr_t*>(table[index] & ~0xFFFULL);
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
    lib::memset(new_pml4, 0, pmm::PAGE_SIZE);

    // Copy kernel mapping (PML4 entry 0)
    new_pml4[0] = g_pml4[0];

    return reinterpret_cast<uintptr_t>(new_pml4);
}

void vmm::map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
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

void vmm::unmap(uintptr_t virt) noexcept {
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    size_t pdpt_idx = (virt >> 30) & 0x1FF;
    size_t pd_idx = (virt >> 21) & 0x1FF;
    size_t pt_idx = (virt >> 12) & 0x1FF;

    uintptr_t* pdpt = get_next_table(g_pml4, pml4_idx, false);
    if (!pdpt) return;
    uintptr_t* pd = get_next_table(pdpt, pdpt_idx, false);
    if (!pd) return;
    uintptr_t* pt = get_next_table(pd, pd_idx, false);
    if (!pt) return;

    pt[pt_idx] = 0;
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm::switch_to(uintptr_t pml4_phys) noexcept {
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uintptr_t vmm::get_active_page_table() noexcept {
    uintptr_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    return current_cr3;
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

} // namespace kernel::memory
