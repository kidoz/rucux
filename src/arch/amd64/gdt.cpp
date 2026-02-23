// SPDX-License-Identifier: MIT
#include <arch/amd64/gdt.hpp>
#include <arch/amd64/tss.hpp>

namespace arch::amd64 {

static gdt_descriptor g_gdt[7]; // Increased for TSS
static gdt_pointer g_gdt_ptr;

// Helper for 16-byte TSS descriptor
static void set_tss_descriptor(int i, uint64_t base, uint32_t limit) noexcept {
    g_gdt[i].limit = limit & 0xFFFF;
    g_gdt[i].base_low = base & 0xFFFF;
    g_gdt[i].base_mid = (base >> 16) & 0xFF;
    g_gdt[i].access = 0x89; // TSS Present, Executable, Ring 0
    g_gdt[i].granularity = ((limit >> 16) & 0x0F);
    g_gdt[i].base_high = (base >> 24) & 0xFF;

    // High 32 bits of base for 64-bit TSS descriptor
    auto* high = reinterpret_cast<uint32_t*>(&g_gdt[i + 1]);
    *high = (base >> 32) & 0xFFFFFFFF;
    *(high + 1) = 0;
}

static void set_descriptor(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) noexcept {
    g_gdt[i].base_low = (base & 0xFFFF);
    g_gdt[i].base_mid = (base >> 16) & 0xFF;
    g_gdt[i].base_high = (base >> 24) & 0xFF;
    g_gdt[i].limit = (limit & 0xFFFF);
    g_gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    g_gdt[i].access = access;
}

extern tss_entry* get_tss() noexcept;

void gdt_init() noexcept {
    set_descriptor(0, 0, 0, 0, 0);                // Null
    set_descriptor(1, 0, 0xFFFFFFFF, 0x9A, 0xA0); // Kernel Code 64
    set_descriptor(2, 0, 0xFFFFFFFF, 0x92, 0xA0); // Kernel Data 64
    set_descriptor(3, 0, 0xFFFFFFFF, 0xF2, 0xA0); // User Data 64
    set_descriptor(4, 0, 0xFFFFFFFF, 0xFA, 0xA0); // User Code 64

    tss_init();
    set_tss_descriptor(5, reinterpret_cast<uintptr_t>(get_tss()), sizeof(tss_entry) - 1);

    g_gdt_ptr.size = sizeof(g_gdt) - 1;
    g_gdt_ptr.offset = reinterpret_cast<uintptr_t>(&g_gdt);

    asm volatile("lgdt %0\n"
                 "pushq $0x08\n"
                 "pushq $1f\n"
                 "lretq\n"
                 "1:\n"
                 "mov $0x10, %%ax\n"
                 "mov %%ax, %%ds\n"
                 "mov %%ax, %%es\n"
                 "mov %%ax, %%fs\n"
                 "mov %%ax, %%gs\n"
                 "mov %%ax, %%ss\n"
                 "mov $0x28, %%ax\n" // TSS Selector (5 * 8 = 0x28)
                 "ltr %%ax\n"
                 :
                 : "m"(g_gdt_ptr)
                 : "rax", "memory");
}

} // namespace arch::amd64
