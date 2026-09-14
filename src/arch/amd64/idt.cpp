// SPDX-License-Identifier: MIT
#include <arch/amd64/apic.hpp>
#include <arch/amd64/idt.hpp>
#include <arch/amd64/io.hpp>
#include <arch/amd64/pic.hpp>
#include <arch/amd64/uart.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/tty.hpp>

namespace arch::amd64 {

static idt_descriptor g_idt[256];
static idt_pointer g_idt_ptr;
static bool g_apic_mode = false;
static bool g_lapic_timer = false;
void idt_use_lapic_timer() noexcept {
    g_lapic_timer = true;
}

void idt_set_apic_mode(bool enabled) noexcept {
    g_apic_mode = enabled;
}

static void do_eoi(uint8_t irq) noexcept {
    if (g_apic_mode || (irq == 0 && g_lapic_timer))
        lapic::send_eoi();
    else
        pic::send_eoi(irq);
}

// ─── IRQ handlers ──────────────────────────────────────────────────────────

extern "C" void irq0_entry();
extern "C" void irq0_handler() noexcept {
    do_eoi(0);
    kernel::cpu::this_cpu()->ticks++;
    if (kernel::cpu::this_cpu()->cpu_id == 0) kernel::time_manager::tick();
    kernel::scheduler::scheduler::schedule();
}

extern "C" void irq1_entry();
extern "C" void irq1_handler() noexcept {
    do_eoi(1);
    kernel::scheduler::scheduler::wake_irq_waiters(1);
    kernel::scheduler::scheduler::schedule();
}

extern "C" void irq4_entry();
extern "C" void irq4_handler() noexcept {
    // Drain the receive FIFO before EOI, including timeout interrupts.
    for (unsigned i = 0; i < 256 && (inb(0x3FD) & 1); ++i)
        kernel::vfs::tty::feed_input(static_cast<char>(inb(0x3F8)));
    do_eoi(4);
    kernel::scheduler::scheduler::schedule();
}

// ─── Exception stubs (defined in switch.S) ─────────────────────────────────

extern "C" void isr_stub_0();
extern "C" void isr_stub_1();
extern "C" void isr_stub_2();
extern "C" void isr_stub_3();
extern "C" void isr_stub_4();
extern "C" void isr_stub_5();
extern "C" void isr_stub_6();
extern "C" void isr_stub_7();
extern "C" void isr_stub_8();
extern "C" void isr_stub_9();
extern "C" void isr_stub_10();
extern "C" void isr_stub_11();
extern "C" void isr_stub_12();
extern "C" void isr_stub_13();
extern "C" void isr_stub_14();
extern "C" void isr_stub_16();
extern "C" void isr_stub_17();
extern "C" void isr_stub_18();
extern "C" void isr_stub_19();

static const char* const exc_names[] = {"#DE Divide",
                                        "#DB Debug",
                                        "NMI",
                                        "#BP Breakpoint",
                                        "#OF Overflow",
                                        "#BR Bound",
                                        "#UD Invalid Opcode",
                                        "#NM No FPU",
                                        "#DF Double Fault",
                                        "Coproc Seg",
                                        "#TS Invalid TSS",
                                        "#NP Seg Not Present",
                                        "#SS Stack Fault",
                                        "#GP General Protection",
                                        "#PF Page Fault",
                                        "(reserved)",
                                        "#MF x87 FPE",
                                        "#AC Alignment",
                                        "#MC Machine Check",
                                        "#XM SIMD FPE"};

extern "C" void isr_handler(uint64_t vector, uint64_t error_code) noexcept {
    if (vector == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        if (kernel::memory::vmm::handle_page_fault(cr2, error_code)) return;
        kernel::print("#PF at {} err={}\n", reinterpret_cast<void*>(cr2), error_code);
    } else if (vector < 20) {
        kernel::print("EXCEPTION {}: {} err={}\n", vector, exc_names[vector], error_code);
    } else {
        kernel::print("ISR vector={} err={}\n", vector, error_code);
    }

    // Dump RIP from the interrupt frame (9 pushes + error code on stack)
    // Don't halt for NMI (vector 2) — it might be spurious
    if (vector == 2) return;

    while (true)
        asm volatile("hlt");
}

// ─── Default handler for unregistered vectors ──────────────────────────────
// This catches spurious PIC IRQs and unexpected device interrupts.
// NON-FATAL: sends EOI and returns so the kernel keeps running.

extern "C" void idt_default_handler() noexcept {
    if (g_apic_mode) {
        lapic::send_eoi();
    } else {
        // Check PIC ISR to see if this is a real or spurious IRQ
        outb(0x20, 0x0B);
        uint8_t isr = inb(0x20);
        if (isr == 0) return; // Spurious — just ignore

        pic::send_eoi(0);
        pic::send_eoi(8);
    }
    // Non-fatal: return to interrupted code
}

// ─── IDT setup ─────────────────────────────────────────────────────────────

static void set_descriptor(uint8_t vector, void* handler, uint8_t flags) noexcept {
    auto addr = reinterpret_cast<uintptr_t>(handler);
    g_idt[vector].offset_low = addr & 0xFFFF;
    g_idt[vector].selector = 0x08;
    g_idt[vector].ist = 0;
    g_idt[vector].flags = flags;
    g_idt[vector].offset_mid = (addr >> 16) & 0xFFFF;
    g_idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
    g_idt[vector].reserved = 0;
}

void idt_init() noexcept {
    // Default: all vectors → non-fatal handler
    for (int i = 0; i < 256; ++i)
        set_descriptor(i, reinterpret_cast<void*>(idt_default_handler), 0x8E);

    // CPU exceptions (0-19) → proper stubs that capture vector + error code
    set_descriptor(0, reinterpret_cast<void*>(isr_stub_0), 0x8E);
    set_descriptor(1, reinterpret_cast<void*>(isr_stub_1), 0x8E);
    set_descriptor(2, reinterpret_cast<void*>(isr_stub_2), 0x8E);
    set_descriptor(3, reinterpret_cast<void*>(isr_stub_3), 0x8E);
    set_descriptor(4, reinterpret_cast<void*>(isr_stub_4), 0x8E);
    set_descriptor(5, reinterpret_cast<void*>(isr_stub_5), 0x8E);
    set_descriptor(6, reinterpret_cast<void*>(isr_stub_6), 0x8E);
    set_descriptor(7, reinterpret_cast<void*>(isr_stub_7), 0x8E);
    set_descriptor(8, reinterpret_cast<void*>(isr_stub_8), 0x8E);
    set_descriptor(9, reinterpret_cast<void*>(isr_stub_9), 0x8E);
    set_descriptor(10, reinterpret_cast<void*>(isr_stub_10), 0x8E);
    set_descriptor(11, reinterpret_cast<void*>(isr_stub_11), 0x8E);
    set_descriptor(12, reinterpret_cast<void*>(isr_stub_12), 0x8E);
    set_descriptor(13, reinterpret_cast<void*>(isr_stub_13), 0x8E);
    set_descriptor(14, reinterpret_cast<void*>(isr_stub_14), 0x8E);
    set_descriptor(16, reinterpret_cast<void*>(isr_stub_16), 0x8E);
    set_descriptor(17, reinterpret_cast<void*>(isr_stub_17), 0x8E);
    set_descriptor(18, reinterpret_cast<void*>(isr_stub_18), 0x8E);
    set_descriptor(19, reinterpret_cast<void*>(isr_stub_19), 0x8E);

    // Hardware IRQs (PIC vectors 0x20-0x2F)
    set_descriptor(0x20, reinterpret_cast<void*>(irq0_entry), 0x8E); // Timer
    set_descriptor(0x21, reinterpret_cast<void*>(irq1_entry), 0x8E); // Keyboard
    set_descriptor(0x24, reinterpret_cast<void*>(irq4_entry), 0x8E); // COM1

    g_idt_ptr.size = sizeof(g_idt) - 1;
    g_idt_ptr.offset = reinterpret_cast<uintptr_t>(&g_idt);

    pic::init();
    pic::unmask(0);
    pic::unmask(1);
    pic::unmask(4);

    asm volatile("lidt %0" : : "m"(g_idt_ptr));
}

} // namespace arch::amd64
