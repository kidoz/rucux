// SPDX-License-Identifier: MIT
//
// ARMv7 LPAE (Large Physical Address Extension) Virtual Memory Manager.
//
// LPAE uses 3-level page tables with 64-bit descriptors:
//   L1 (TTBR → 4 entries, each covers 1GB)
//   L2 (512 entries, each covers 2MB — can be a block or table pointer)
//   L3 (512 entries, each covers 4KB)
//
// We use TTBR0 for user space (low addresses) and TTBR1 for kernel (high addresses).
// For simplicity in this implementation, we use a single TTBR0 mapping
// covering both kernel and user, with domain/permission-based separation.

#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vma.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

static constexpr size_t PAGE_SIZE = 4096;

// LPAE descriptor bits
static constexpr uint64_t LPAE_VALID = (1ULL << 0);
static constexpr uint64_t LPAE_TABLE = (1ULL << 1); // L1/L2: next-level table
static constexpr uint64_t LPAE_BLOCK = (0ULL << 1); // L1/L2: block mapping (with VALID)
static constexpr uint64_t LPAE_PAGE = (1ULL << 1);  // L3: page mapping (with VALID)

// Access permissions (AP[2:1] at bits [7:6])
static constexpr uint64_t LPAE_AP_RW_PL1 = (0ULL << 6); // PL1 R/W, PL0 no access
static constexpr uint64_t LPAE_AP_RW_ALL = (1ULL << 6); // PL1 R/W, PL0 R/W
static constexpr uint64_t LPAE_AP_RO_PL1 = (2ULL << 6); // PL1 R/O, PL0 no access
static constexpr uint64_t LPAE_AP_RO_ALL = (3ULL << 6); // PL1 R/O, PL0 R/O

// Memory attributes (AttrIndx[2:0] at bits [4:2])
// We configure MAIR to have:
//   Index 0 = Device-nGnRnE (strongly ordered)
//   Index 1 = Normal, Write-Back, Cacheable
static constexpr uint64_t LPAE_ATTR_DEVICE = (0ULL << 2);
static constexpr uint64_t LPAE_ATTR_NORMAL = (1ULL << 2);

// Shareability (SH[1:0] at bits [9:8])
static constexpr uint64_t LPAE_SH_INNER = (3ULL << 8);

// Access flag (AF, bit 10) — must be set or we get access flag faults
static constexpr uint64_t LPAE_AF = (1ULL << 10);

// Execute-never (XN, bit 54 for PL1, PXN bit 53)
static constexpr uint64_t LPAE_XN = (1ULL << 54);
static constexpr uint64_t LPAE_PXN = (1ULL << 53);

// L1 table: 4 entries (for 32-bit VA space: 4GB / 1GB = 4)
// Each L1 entry covers 1GB
static uint64_t* g_l1_table = nullptr;

// Convert our generic page_flags to LPAE descriptor bits
static uint64_t flags_to_lpae(page_flags flags) noexcept {
    uint64_t bits = LPAE_AF | LPAE_SH_INNER | LPAE_ATTR_NORMAL;

    if (static_cast<uint64_t>(flags) & static_cast<uint64_t>(page_flags::USER)) {
        bits |= (static_cast<uint64_t>(flags) & static_cast<uint64_t>(page_flags::WRITABLE)) ? LPAE_AP_RW_ALL
                                                                                             : LPAE_AP_RO_ALL;
    } else {
        bits |= (static_cast<uint64_t>(flags) & static_cast<uint64_t>(page_flags::WRITABLE)) ? LPAE_AP_RW_PL1
                                                                                             : LPAE_AP_RO_PL1;
    }

    if (static_cast<uint64_t>(flags) & static_cast<uint64_t>(page_flags::NO_EXECUTE)) bits |= LPAE_XN;

    return bits;
}

// Allocate a zeroed page for a page table
static uint64_t* alloc_table() noexcept {
    void* p = pmm::alloc_page();
    if (!p) return nullptr;
    lib::memset(p, 0, PAGE_SIZE);
    return reinterpret_cast<uint64_t*>(p);
}

// Get or create next-level table from a descriptor
static uint64_t* get_next_table(uint64_t* table, size_t index, bool create) noexcept {
    if (table[index] & LPAE_VALID) {
        // Extract physical address (bits [39:12])
        uintptr_t phys = static_cast<uintptr_t>(table[index] & 0xFFFFFFF000ULL);
        return reinterpret_cast<uint64_t*>(phys);
    }
    if (!create) return nullptr;

    auto* next = alloc_table();
    if (!next) return nullptr;

    table[index] = reinterpret_cast<uintptr_t>(next) | LPAE_VALID | LPAE_TABLE;
    return next;
}

void vmm::init() noexcept {
    // Allocate L1 table (must be 32-byte aligned for LPAE, page-aligned is fine)
    g_l1_table = alloc_table();
    if (!g_l1_table) {
        kernel::print("ARMv7 VMM: Failed to allocate L1 table!\n");
        return;
    }

    // Set up MAIR (Memory Attribute Indirection Register)
    //   Attr0 = 0x00 (Device-nGnRnE)
    //   Attr1 = 0xFF (Normal, Write-Back, Write-Allocate, Inner/Outer)
    uint32_t mair = 0x0000FF00;
    asm volatile("mcr p15, 0, %0, c10, c2, 0" ::"r"(mair)); // MAIR0

    // Identity-map the first 4GB using 2MB blocks (L2 block descriptors)
    // This gives us a working kernel mapping. User mappings will be added later.
    for (uint32_t gb = 0; gb < 4; ++gb) {
        auto* l2 = alloc_table();
        if (!l2) break;

        for (uint32_t i = 0; i < 512; ++i) {
            uintptr_t phys = (static_cast<uintptr_t>(gb) << 30) | (static_cast<uintptr_t>(i) << 21);
            // 2MB block descriptor: VALID + BLOCK(0) + attributes
            l2[i] = phys | LPAE_VALID | LPAE_AF | LPAE_SH_INNER | LPAE_ATTR_NORMAL | LPAE_AP_RW_PL1;
        }

        g_l1_table[gb] = reinterpret_cast<uintptr_t>(l2) | LPAE_VALID | LPAE_TABLE;
    }

    // Configure TTBCR for LPAE mode
    // EAE (bit 31) = 1 enables LPAE
    // T0SZ = 0 (TTBR0 maps full 32-bit VA range)
    uint32_t ttbcr = (1U << 31);                            // EAE=1, T0SZ=0
    asm volatile("mcr p15, 0, %0, c2, c0, 2" ::"r"(ttbcr)); // TTBCR

    // Set TTBR0 (64-bit register via MCRR)
    uint64_t ttbr0 = reinterpret_cast<uintptr_t>(g_l1_table);
    uint32_t lo = static_cast<uint32_t>(ttbr0);
    uint32_t hi = static_cast<uint32_t>(ttbr0 >> 32);
    asm volatile("mcrr p15, 0, %0, %1, c2" ::"r"(lo), "r"(hi)); // TTBR0

    // Enable MMU (if not already enabled by bootloader)
    uint32_t sctlr;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= (1 << 0);  // M: MMU enable
    sctlr |= (1 << 2);  // C: Data cache enable
    sctlr |= (1 << 12); // I: Instruction cache enable
    asm volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr));

    // Invalidate TLB
    asm volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(0)); // TLBIALL
    asm volatile("dsb sy; isb" ::: "memory");

    kernel::print("ARMv7 VMM: LPAE enabled, L1 at {}\n", reinterpret_cast<void*>(g_l1_table));
}

uintptr_t vmm::create_address_space() noexcept {
    auto* new_l1 = alloc_table();
    if (!new_l1) return 0;

    // Copy kernel mappings (all 4 L1 entries)
    for (int i = 0; i < 4; ++i)
        new_l1[i] = g_l1_table[i];

    return reinterpret_cast<uintptr_t>(new_l1);
}

void vmm::map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    // 4KB page mapping through L1 → L2 → L3
    size_t l1_idx = (virt >> 30) & 0x3;   // 2 bits (4 entries)
    size_t l2_idx = (virt >> 21) & 0x1FF; // 9 bits
    size_t l3_idx = (virt >> 12) & 0x1FF; // 9 bits

    // Use current TTBR0
    uint32_t lo, hi;
    asm volatile("mrrc p15, 0, %0, %1, c2" : "=r"(lo), "=r"(hi));
    auto* l1 = reinterpret_cast<uint64_t*>(lo);

    auto* l2 = get_next_table(l1, l1_idx, true);
    if (!l2) return;

    // If L2 entry is a 2MB block, we need to split it into a table
    if ((l2[l2_idx] & LPAE_VALID) && !(l2[l2_idx] & LPAE_TABLE)) {
        // Split: create L3 table and fill with 512 4KB pages
        auto* l3 = alloc_table();
        if (!l3) return;
        uintptr_t block_phys = static_cast<uintptr_t>(l2[l2_idx] & 0xFFFFFFF000ULL) & ~0x1FFFFFULL;
        uint64_t old_attrs = l2[l2_idx] & 0xFFF0000000000FFFULL;
        for (size_t i = 0; i < 512; ++i)
            l3[i] = (block_phys + i * PAGE_SIZE) | (old_attrs & ~LPAE_TABLE) | LPAE_VALID | LPAE_PAGE;
        l2[l2_idx] = reinterpret_cast<uintptr_t>(l3) | LPAE_VALID | LPAE_TABLE;
    }

    auto* l3 = get_next_table(l2, l2_idx, true);
    if (!l3) return;

    l3[l3_idx] = (phys & ~0xFFFULL) | LPAE_VALID | LPAE_PAGE | flags_to_lpae(flags);

    // Invalidate TLB for this address
    asm volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"(virt)); // TLBIMVA
    asm volatile("dsb sy; isb" ::: "memory");
}

void vmm::map_2mb(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    size_t l1_idx = (virt >> 30) & 0x3;
    size_t l2_idx = (virt >> 21) & 0x1FF;

    uint32_t lo, hi;
    asm volatile("mrrc p15, 0, %0, %1, c2" : "=r"(lo), "=r"(hi));
    auto* l1 = reinterpret_cast<uint64_t*>(lo);

    auto* l2 = get_next_table(l1, l1_idx, true);
    if (!l2) return;

    // 2MB block descriptor (VALID + not TABLE = block)
    l2[l2_idx] = (phys & ~0x1FFFFFULL) | LPAE_VALID | flags_to_lpae(flags);

    asm volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"(virt));
    asm volatile("dsb sy; isb" ::: "memory");
}

void vmm::unmap(uintptr_t virt) noexcept {
    size_t l1_idx = (virt >> 30) & 0x3;
    size_t l2_idx = (virt >> 21) & 0x1FF;
    size_t l3_idx = (virt >> 12) & 0x1FF;

    uint32_t lo, hi;
    asm volatile("mrrc p15, 0, %0, %1, c2" : "=r"(lo), "=r"(hi));
    auto* l1 = reinterpret_cast<uint64_t*>(lo);

    auto* l2 = get_next_table(l1, l1_idx, false);
    if (!l2) return;

    // Check if L2 entry is a 2MB block
    if ((l2[l2_idx] & LPAE_VALID) && !(l2[l2_idx] & LPAE_TABLE)) {
        l2[l2_idx] = 0;
    } else {
        auto* l3 = get_next_table(l2, l2_idx, false);
        if (!l3) return;
        l3[l3_idx] = 0;
    }

    asm volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"(virt));
    asm volatile("dsb sy; isb" ::: "memory");
}

void vmm::switch_to(uintptr_t l1_phys) noexcept {
    uint32_t lo = static_cast<uint32_t>(l1_phys);
    uint32_t hi = 0;
    asm volatile("mcrr p15, 0, %0, %1, c2" ::"r"(lo), "r"(hi)); // TTBR0
    asm volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(0));         // TLBIALL
    asm volatile("dsb sy; isb" ::: "memory");
}

uintptr_t vmm::get_active_page_table() noexcept {
    uint32_t lo, hi;
    asm volatile("mrrc p15, 0, %0, %1, c2" : "=r"(lo), "=r"(hi));
    return static_cast<uintptr_t>(lo);
}

uintptr_t vmm::get_phys(uintptr_t virt) noexcept {
    size_t l1_idx = (virt >> 30) & 0x3;
    size_t l2_idx = (virt >> 21) & 0x1FF;
    size_t l3_idx = (virt >> 12) & 0x1FF;

    uint32_t lo, hi;
    asm volatile("mrrc p15, 0, %0, %1, c2" : "=r"(lo), "=r"(hi));
    auto* l1 = reinterpret_cast<uint64_t*>(lo);

    auto* l2 = get_next_table(l1, l1_idx, false);
    if (!l2) return 0;

    if ((l2[l2_idx] & LPAE_VALID) && !(l2[l2_idx] & LPAE_TABLE)) {
        return static_cast<uintptr_t>((l2[l2_idx] & 0xFFFFFFE00000ULL) | (virt & 0x1FFFFF));
    }

    auto* l3 = get_next_table(l2, l2_idx, false);
    if (!l3) return 0;

    if (l3[l3_idx] & LPAE_VALID) {
        return static_cast<uintptr_t>((l3[l3_idx] & 0xFFFFFFFFF000ULL) | (virt & 0xFFF));
    }
    return 0;
}

void vmm::disable_write_protect() noexcept {
    // ARMv7 doesn't have a WP bit like x86. Access is controlled per-page.
    // This is a no-op; use map() with WRITABLE flag to change permissions.
}

void vmm::enable_write_protect() noexcept {
    // No-op on ARMv7 — see above.
}

bool vmm::handle_page_fault(uintptr_t fault_addr, uint64_t) noexcept {
    // Look up VMA for demand paging
    extern vma_manager g_vma;
    extern bool g_vma_initialized;
    if (!g_vma_initialized) return false;

    vma* v = g_vma.find(fault_addr);
    if (!v) return false;

    void* phys = pmm::alloc_page();
    if (!phys) return false;

    lib::memset(phys, 0, PAGE_SIZE);

    page_flags flags = page_flags::PRESENT | page_flags::USER;
    if (v->prot & 0x02) flags = flags | page_flags::WRITABLE;

    uintptr_t page_virt = fault_addr & ~0xFFFULL;
    vmm::map(page_virt, reinterpret_cast<uintptr_t>(phys), flags);
    return true;
}

} // namespace kernel::memory
