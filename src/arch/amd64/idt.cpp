// SPDX-License-Identifier: MIT
#include <arch/amd64/idt.hpp>
#include <arch/amd64/pic.hpp>
#include <arch/amd64/uart.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>

namespace arch::amd64 {

static idt_descriptor g_idt[256];
static idt_pointer g_idt_ptr;

extern "C" void irq0_entry();
extern "C" void irq0_handler() noexcept {
    pic::send_eoi(0);
    kernel::scheduler::scheduler::schedule();
}

extern "C" void irq1_entry();
extern "C" void irq1_handler() noexcept {
    pic::send_eoi(1);
    kernel::scheduler::scheduler::wake_irq_waiters(1);
    // Optionally schedule immediately to wake up the blocked driver faster
    kernel::scheduler::scheduler::schedule();
}

extern "C" void irq4_handler() noexcept {
    // UART Interrupt occurred
    // We should notify the Console Server here
    pic::send_eoi(4);
}

extern "C" void isr_stub_14();
extern "C" void isr_handler(uint64_t vector, uint64_t error_code) noexcept {
    if (vector == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        kernel::print("Page Fault at {} (error code: {})\n", reinterpret_cast<void*>(cr2), error_code);
    } else {
        kernel::print("Unhandled interrupt: {}\n", vector);
    }
    while (true)
        asm volatile("hlt");
}

extern "C" void idt_default_handler() noexcept {
    kernel::print("Unhandled interrupt\n");
    while (true)
        asm volatile("hlt");
}

static void set_descriptor(uint8_t vector, void* handler, uint8_t flags) noexcept {
    auto addr = reinterpret_cast<uintptr_t>(handler);
    g_idt[vector].offset_low = addr & 0xFFFF;
    g_idt[vector].selector = 0x08; // Kernel Code selector
    g_idt[vector].ist = 0;
    g_idt[vector].flags = flags;
    g_idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    g_idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    g_idt[vector].reserved = 0;
}

void idt_init() noexcept {
    for (int i = 0; i < 256; ++i) {
        set_descriptor(i, reinterpret_cast<void*>(idt_default_handler), 0x8E);
    }

    set_descriptor(14, reinterpret_cast<void*>(isr_stub_14), 0x8E);
    set_descriptor(0x20, reinterpret_cast<void*>(irq0_entry), 0x8E);
    set_descriptor(0x21, reinterpret_cast<void*>(irq1_entry), 0x8E);
    set_descriptor(0x24, reinterpret_cast<void*>(irq4_handler), 0x8E);

    g_idt_ptr.size = sizeof(g_idt) - 1;
    g_idt_ptr.offset = reinterpret_cast<uintptr_t>(&g_idt);

    pic::init();
    pic::unmask(0); // Timer IRQ 0
    pic::unmask(1); // Keyboard IRQ 1
    pic::unmask(4); // COM1 IRQ 4

    asm volatile("lidt %0" : : "m"(g_idt_ptr));
}

} // namespace arch::amd64
