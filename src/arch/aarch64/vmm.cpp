// SPDX-License-Identifier: MIT
//
// AArch64 stage-1 virtual memory manager.
//
// Configuration: 4 KiB granule, 39-bit VA (T0SZ = 25), three levels.
//   L1 — 512 entries, 1 GiB each  (VA[38:30])
//   L2 — 512 entries, 2 MiB each  (VA[29:21])   may be a block or a table
//   L3 — 512 entries, 4 KiB each  (VA[20:12])
//
// Three levels rather than four keeps the walk short while still covering
// 512 GiB — far beyond the 2 GiB on an Odroid C2. TTBR0_EL1 holds the active
// table; the kernel currently runs identity-mapped, so TTBR1 is unused.

#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

// RAM geometry from the board manifest. Everything outside this window is
// mapped as Device-nGnRnE: mapping MMIO as Normal cacheable would let the CPU
// merge, reorder, or cache UART and GIC accesses.
#ifndef RUCUX_RAM_BASE
#define RUCUX_RAM_BASE 0x40000000
#endif
#ifndef RUCUX_RAM_SIZE
#define RUCUX_RAM_SIZE 0x20000000
#endif

namespace kernel::memory {

namespace {

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t ENTRIES = 512;
constexpr uint64_t BLOCK_2MB = 0x200000ULL;

// Descriptor bits
constexpr uint64_t DESC_VALID = (1ULL << 0);
constexpr uint64_t DESC_TABLE = (1ULL << 1); // L1/L2: table pointer; L3: page
constexpr uint64_t DESC_BLOCK = (0ULL << 1); // L1/L2: block mapping

// AttrIndx[2:0] selects a MAIR_EL1 field.
constexpr uint64_t ATTR_DEVICE = (0ULL << 2); // MAIR Attr0
constexpr uint64_t ATTR_NORMAL = (1ULL << 2); // MAIR Attr1

// AP[2:1] at bits [7:6]
constexpr uint64_t AP_RW_EL1 = (0ULL << 6);
constexpr uint64_t AP_RW_ALL = (1ULL << 6);
constexpr uint64_t AP_RO_EL1 = (2ULL << 6);
constexpr uint64_t AP_RO_ALL = (3ULL << 6);

constexpr uint64_t SH_INNER = (3ULL << 8);
constexpr uint64_t DESC_AF = (1ULL << 10); // Access flag; without it every access faults
constexpr uint64_t DESC_PXN = (1ULL << 53);
constexpr uint64_t DESC_UXN = (1ULL << 54);

// Output address field of a descriptor, bits [47:12].
constexpr uint64_t ADDR_MASK = 0x0000FFFFFFFFF000ULL;

uint64_t* g_l1_table = nullptr;

bool is_ram(uint64_t phys) noexcept {
    constexpr uint64_t base = static_cast<uint64_t>(RUCUX_RAM_BASE);
    constexpr uint64_t size = static_cast<uint64_t>(RUCUX_RAM_SIZE);
    return phys >= base && phys < base + size;
}

uint64_t flags_to_desc(page_flags flags) noexcept {
    const auto raw = static_cast<uint64_t>(flags);
    uint64_t bits = DESC_AF | SH_INNER | ATTR_NORMAL;

    if (raw & static_cast<uint64_t>(page_flags::USER)) {
        bits |= (raw & static_cast<uint64_t>(page_flags::WRITABLE)) ? AP_RW_ALL : AP_RO_ALL;
    } else {
        bits |= (raw & static_cast<uint64_t>(page_flags::WRITABLE)) ? AP_RW_EL1 : AP_RO_EL1;
        bits |= DESC_UXN; // kernel pages are never executable by EL0
    }

    if (raw & static_cast<uint64_t>(page_flags::NO_EXECUTE))
        bits |= DESC_UXN | DESC_PXN;

    return bits;
}

uint64_t* alloc_table() noexcept {
    void* p = pmm::alloc_page();
    if (!p) return nullptr;
    lib::memset(p, 0, PAGE_SIZE);
    return reinterpret_cast<uint64_t*>(p);
}

uint64_t* active_l1() noexcept {
    uint64_t ttbr0 = 0;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
    return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(ttbr0 & ADDR_MASK));
}

uint64_t* next_table(uint64_t* table, size_t index, bool create) noexcept {
    if ((table[index] & DESC_VALID) && (table[index] & DESC_TABLE))
        return reinterpret_cast<uint64_t*>(static_cast<uintptr_t>(table[index] & ADDR_MASK));
    if (!create) return nullptr;

    auto* fresh = alloc_table();
    if (!fresh) return nullptr;
    table[index] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fresh)) | DESC_VALID | DESC_TABLE;
    return fresh;
}

void invalidate(uintptr_t virt) noexcept {
    asm volatile("dsb ishst" ::: "memory");
    asm volatile("tlbi vaae1is, %0" ::"r"(virt >> 12) : "memory");
    asm volatile("dsb ish; isb" ::: "memory");
}

} // namespace

void vmm::init() noexcept {
    g_l1_table = alloc_table();
    if (!g_l1_table) {
        kernel::print("AArch64 VMM: failed to allocate L1 table\n");
        return;
    }

    // MAIR_EL1: Attr0 = Device-nGnRnE (0x00), Attr1 = Normal WB/WA (0xFF).
    constexpr uint64_t mair = 0xFF00ULL;
    asm volatile("msr mair_el1, %0" ::"r"(mair));

    // Identity-map the low 4 GiB with 2 MiB blocks. That covers DRAM plus the
    // MMIO windows on both QEMU `virt` and the Amlogic S905.
    constexpr uint64_t MAPPED_LIMIT = 0x100000000ULL;
    for (uint64_t gb = 0; gb < MAPPED_LIMIT / (1ULL << 30); ++gb) {
        auto* l2 = alloc_table();
        if (!l2) {
            kernel::print("AArch64 VMM: out of memory building L2 for GiB {}\n", static_cast<uint32_t>(gb));
            break;
        }

        for (uint64_t i = 0; i < ENTRIES; ++i) {
            const uint64_t phys = (gb << 30) | (i * BLOCK_2MB);
            uint64_t desc = phys | DESC_VALID | DESC_BLOCK | DESC_AF | AP_RW_EL1 | DESC_UXN;
            if (is_ram(phys)) {
                desc |= ATTR_NORMAL | SH_INNER;
            } else {
                // Device memory must not be speculatively executed from.
                desc |= ATTR_DEVICE | DESC_PXN;
            }
            l2[i] = desc;
        }

        g_l1_table[gb] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(l2)) | DESC_VALID | DESC_TABLE;
    }

    // TCR_EL1: 39-bit VA on TTBR0, 4 KiB granule, inner-shareable write-back
    // walks, 40-bit intermediate physical addresses.
    constexpr uint64_t T0SZ = 25;
    constexpr uint64_t tcr = T0SZ                // TTBR0 region size
                             | (0ULL << 14)      // TG0 = 4 KiB granule
                             | (3ULL << 12)      // SH0 = inner shareable
                             | (1ULL << 10)      // ORGN0 = WB WA
                             | (1ULL << 8)       // IRGN0 = WB WA
                             | (1ULL << 23)      // EPD1: no TTBR1 walks
                             | (2ULL << 32);     // IPS = 40-bit PA
    asm volatile("msr tcr_el1, %0" ::"r"(tcr));

    const auto ttbr0 = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_l1_table));
    asm volatile("msr ttbr0_el1, %0" ::"r"(ttbr0));

    asm volatile("dsb ish" ::: "memory");
    asm volatile("tlbi vmalle1" ::: "memory");
    asm volatile("dsb ish; isb" ::: "memory");

    uint64_t sctlr = 0;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);  // M: MMU enable
    sctlr |= (1ULL << 2);  // C: data cache
    sctlr |= (1ULL << 12); // I: instruction cache
    asm volatile("msr sctlr_el1, %0" ::"r"(sctlr) : "memory");
    asm volatile("isb" ::: "memory");

    kernel::print("AArch64 VMM: MMU enabled, L1 at {}\n", reinterpret_cast<void*>(g_l1_table));
}

uintptr_t vmm::create_address_space() noexcept {
    auto* fresh = alloc_table();
    if (!fresh) return 0;
    for (size_t i = 0; i < ENTRIES; ++i)
        fresh[i] = g_l1_table[i]; // share kernel mappings
    return reinterpret_cast<uintptr_t>(fresh);
}

void vmm::map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    const size_t l1_idx = (virt >> 30) & 0x1FF;
    const size_t l2_idx = (virt >> 21) & 0x1FF;
    const size_t l3_idx = (virt >> 12) & 0x1FF;

    auto* l1 = active_l1();
    auto* l2 = next_table(l1, l1_idx, true);
    if (!l2) return;

    // Split a 2 MiB block into 4 KiB pages if one covers this address.
    if ((l2[l2_idx] & DESC_VALID) && !(l2[l2_idx] & DESC_TABLE)) {
        auto* l3 = alloc_table();
        if (!l3) return;
        const uint64_t block_phys = l2[l2_idx] & ADDR_MASK & ~(BLOCK_2MB - 1);
        const uint64_t attrs = l2[l2_idx] & ~ADDR_MASK & ~DESC_TABLE;
        for (size_t i = 0; i < ENTRIES; ++i)
            l3[i] = (block_phys + i * PAGE_SIZE) | attrs | DESC_VALID | DESC_TABLE;
        l2[l2_idx] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(l3)) | DESC_VALID | DESC_TABLE;
    }

    auto* l3 = next_table(l2, l2_idx, true);
    if (!l3) return;

    // At L3, bit 1 set means "page" rather than "reserved".
    l3[l3_idx] = (static_cast<uint64_t>(phys) & ADDR_MASK) | DESC_VALID | DESC_TABLE | flags_to_desc(flags);
    invalidate(virt);
}

void vmm::map_2mb(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    const size_t l1_idx = (virt >> 30) & 0x1FF;
    const size_t l2_idx = (virt >> 21) & 0x1FF;

    auto* l2 = next_table(active_l1(), l1_idx, true);
    if (!l2) return;

    l2[l2_idx] = (static_cast<uint64_t>(phys) & ~(BLOCK_2MB - 1)) | DESC_VALID | DESC_BLOCK | flags_to_desc(flags);
    invalidate(virt);
}

void vmm::unmap(uintptr_t virt) noexcept {
    const size_t l1_idx = (virt >> 30) & 0x1FF;
    const size_t l2_idx = (virt >> 21) & 0x1FF;
    const size_t l3_idx = (virt >> 12) & 0x1FF;

    auto* l2 = next_table(active_l1(), l1_idx, false);
    if (!l2) return;

    if ((l2[l2_idx] & DESC_VALID) && !(l2[l2_idx] & DESC_TABLE)) {
        l2[l2_idx] = 0;
    } else {
        auto* l3 = next_table(l2, l2_idx, false);
        if (!l3) return;
        l3[l3_idx] = 0;
    }
    invalidate(virt);
}

void vmm::switch_to(uintptr_t l1_phys) noexcept {
    asm volatile("msr ttbr0_el1, %0" ::"r"(static_cast<uint64_t>(l1_phys)) : "memory");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("tlbi vmalle1" ::: "memory");
    asm volatile("dsb ish; isb" ::: "memory");
}

uintptr_t vmm::get_active_page_table() noexcept {
    return reinterpret_cast<uintptr_t>(active_l1());
}

uintptr_t vmm::get_phys(uintptr_t virt) noexcept {
    const size_t l1_idx = (virt >> 30) & 0x1FF;
    const size_t l2_idx = (virt >> 21) & 0x1FF;
    const size_t l3_idx = (virt >> 12) & 0x1FF;

    auto* l2 = next_table(active_l1(), l1_idx, false);
    if (!l2) return 0;

    if ((l2[l2_idx] & DESC_VALID) && !(l2[l2_idx] & DESC_TABLE))
        return static_cast<uintptr_t>((l2[l2_idx] & ADDR_MASK & ~(BLOCK_2MB - 1)) | (virt & (BLOCK_2MB - 1)));

    auto* l3 = next_table(l2, l2_idx, false);
    if (!l3) return 0;
    if (!(l3[l3_idx] & DESC_VALID)) return 0;

    return static_cast<uintptr_t>((l3[l3_idx] & ADDR_MASK) | (virt & 0xFFF));
}

void vmm::disable_write_protect() noexcept {
    // No AArch64 equivalent of x86 CR0.WP — permissions are per-descriptor.
}

void vmm::enable_write_protect() noexcept {
    // See above.
}

bool vmm::handle_page_fault(uintptr_t, uint64_t) noexcept {
    // Demand paging resolves faults against a process VMA set, which requires
    // the scheduler and a userspace to fault. Neither exists on AArch64 yet, so
    // every fault here is a genuine kernel bug and must not be papered over —
    // returning false lets the exception handler report ESR/ELR instead.
    return false;
}

} // namespace kernel::memory
