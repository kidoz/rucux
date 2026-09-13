// SPDX-License-Identifier: MIT
#include <arch/aarch64/console.hpp>
#include <arch/aarch64/exception.hpp>
#include <arch/aarch64/gic.hpp>
#include <arch/aarch64/smp.hpp>
#include <arch/aarch64/syscall.hpp>
#include <arch/aarch64/timer.hpp>
#include <arch/aarch64/uart.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/fdt.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/process/elf.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/ramfs.hpp>
#include <kernel/vfs/tty.hpp>
#include <kernel/vfs/vfs.hpp>

#include <generated_embedded_apps.hpp>
#include <kernel/process/spawn.hpp>
#include <stdint.h>

// Hardware defaults come from the board manifest via meson (see board.yaml).
// They are the authority whenever firmware passes no device tree.
#ifndef RUCUX_UART_BASE
#define RUCUX_UART_BASE 0x09000000 // QEMU `virt` PL011
#endif
#ifndef RUCUX_UART_PL011
#define RUCUX_UART_PL011 1
#endif
#ifndef RUCUX_GIC_DIST_BASE
#define RUCUX_GIC_DIST_BASE 0x08000000
#endif
#ifndef RUCUX_GIC_CPU_BASE
#define RUCUX_GIC_CPU_BASE 0x08010000
#endif
#ifndef RUCUX_RAM_BASE
#define RUCUX_RAM_BASE 0x40000000
#endif
#ifndef RUCUX_RAM_SIZE
#define RUCUX_RAM_SIZE 0x20000000
#endif
#ifndef RUCUX_CPU_COUNT
#define RUCUX_CPU_COUNT 1
#endif

extern "C" void switch_context(uintptr_t* old_stack, uintptr_t new_stack) noexcept;
extern "C" void jump_to_user_space(void* entry, void* stack, void* arg);

namespace {

// Round-trip test for switch.S. scheduler::init() only builds the idle thread;
// it never switches, so the assembly would otherwise ship unexercised.
alignas(16) uint8_t g_probe_stack[4096];
volatile bool g_probe_ran = false;
uintptr_t g_probe_return_sp = 0;

// Entry for the probe context. Never returns normally — its saved x30 slot is
// zero — so it must switch back explicitly.
void probe_entry() noexcept {
    g_probe_ran = true;
    switch_context(nullptr, g_probe_return_sp);
}

void verify_context_switch() noexcept {
    const auto top = (reinterpret_cast<uintptr_t>(g_probe_stack) + sizeof(g_probe_stack)) & ~0xFULL;
    auto* frame = reinterpret_cast<uint64_t*>(top) - 12;
    for (int i = 0; i < 12; ++i)
        frame[i] = 0;
    frame[11] = reinterpret_cast<uint64_t>(&probe_entry); // x30, where `ret` lands

    // Switches away, probe_entry runs, then switches back to just after this call.
    switch_context(&g_probe_return_sp, reinterpret_cast<uintptr_t>(frame));

    if (g_probe_ran) {
        kernel::print("Scheduler: context switch round-trip verified\n");
    } else {
        kernel::print("Scheduler: CONTEXT SWITCH FAILED\n");
    }
}

// Runs as a scheduled kernel thread. Reaching this proves the timer tick
// actually drove schedule() into a context other than idle — preemption end to
// end, not just a switch primitive that works in isolation.
void preempt_probe() noexcept {
    kernel::print("Scheduler: preemption reached a scheduled thread\n");
    for (;;)
        asm volatile("wfi");
}

// Minimal root filesystem so an fd can resolve to something writable.
void init_rootfs() noexcept {
    kernel::vfs::vfs_manager::init();
    auto* root = kernel::vfs::ramfs::create_root();
    kernel::vfs::vfs_manager::set_root(root); // without this get_root() returns null
    auto* dev = kernel::vfs::ramfs::create_directory(root, "dev");
    kernel::vfs::ramfs::attach_node(dev, kernel::vfs::tty::create());
    auto* bin = kernel::vfs::ramfs::create_directory(root, "bin");
    kernel::boot::populate_embedded_binaries(bin);
}

void user_hello() noexcept {
    kernel::boot::start_initial_processes();
    kernel::scheduler::scheduler::exit();
}

// Confirm translation is actually live. The SCTLR_EL1.M bit only says the MMU
// was switched on; walking a known address back through the tables proves the
// descriptors we wrote are the ones the hardware is using.
void verify_mmu() noexcept {
    uint64_t sctlr = 0;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    const bool mmu_on = (sctlr & 1) != 0;

    // The identity map means a kernel address must translate to itself. Use a
    // stack address: it is in RAM and definitely mapped.
    volatile uint64_t probe = 0;
    const auto virt = reinterpret_cast<uintptr_t>(const_cast<uint64_t*>(&probe));
    const uintptr_t phys = kernel::memory::vmm::get_phys(virt);

    kernel::print("MMU: enabled={}, walk {} -> {}\n", mmu_on ? 1 : 0, reinterpret_cast<void*>(virt),
                  reinterpret_cast<void*>(phys));

    if (mmu_on && phys == virt) {
        kernel::print("MMU: identity translation verified\n");
    } else {
        kernel::print("MMU: TRANSLATION MISMATCH — page tables disagree with hardware\n");
    }
}

} // namespace

extern "C" {

void kernel_main(uint64_t fdt_addr) {
    using namespace arch::aarch64;

    // Vectors first. Until VBAR_EL1 is set, any fault vectors to address 0 and
    // spins on undefined instructions, turning a diagnosable abort into a
    // silent hang.
    exceptions_init();

    bool has_fdt = fdt_addr != 0 && kernel::fdt::init(reinterpret_cast<void*>(fdt_addr));

    // Console before anything else that can fail, so failures are visible.
    uintptr_t uart_base = RUCUX_UART_BASE;
    bool is_pl011 = RUCUX_UART_PL011 != 0;
    if (has_fdt) {
        uintptr_t fdt_uart = 0;
        bool fdt_pl011 = false;
        if (kernel::fdt::get_uart(&fdt_uart, &fdt_pl011)) {
            uart_base = fdt_uart;
            is_pl011 = fdt_pl011;
        }
    }
    uart::init_dynamic(uart_base, is_pl011);
    console::init_early();

    kernel::print("rucux (aarch64) Initialized!\n");
    kernel::print("FDT: {}\n", reinterpret_cast<void*>(static_cast<uintptr_t>(fdt_addr)));

    uint64_t current_el = 0;
    asm volatile("mrs %0, CurrentEL" : "=r"(current_el));
    kernel::print("Running at EL{}\n", static_cast<uint32_t>((current_el >> 2) & 3));

    uintptr_t gic_dist = RUCUX_GIC_DIST_BASE;
    uintptr_t gic_cpu = RUCUX_GIC_CPU_BASE;
    uint64_t ram_base = RUCUX_RAM_BASE;
    uint64_t ram_size = RUCUX_RAM_SIZE;

    if (has_fdt) {
        uint64_t mem_base = 0;
        uint64_t mem_size = 0;
        if (kernel::fdt::get_memory(&mem_base, &mem_size)) {
            kernel::print("FDT Memory: base={}, size={} MB\n",
                          reinterpret_cast<void*>(static_cast<uintptr_t>(mem_base)),
                          static_cast<uint32_t>(mem_size / (1024 * 1024)));
            ram_base = mem_base;
            ram_size = mem_size;
        } else {
            kernel::print("WARNING: no memory node in FDT\n");
        }

        if (!kernel::fdt::get_gic(&gic_dist, &gic_cpu)) kernel::print("WARNING: no GIC node in FDT; using defaults\n");
    } else {
        kernel::print("No FDT; using board manifest defaults\n");
    }

    // Reserve the low 16 MiB of RAM for the kernel image, boot stack, and any
    // blob firmware left behind. Everything above is handed to the allocator.
    constexpr uint64_t RESERVED = 0x1000000;
    if (ram_size > RESERVED) {
        kernel::memory::pmm::memory_map_entry entries[1];
        entries[0].base = ram_base + RESERVED;
        entries[0].length = ram_size - RESERVED;
        entries[0].type = 1;
        kernel::memory::pmm::init(entries, 1);
        kernel::print("PMM: {} MB free\n", (kernel::memory::pmm::get_free_pages() * 4096) / (1024 * 1024));

        kernel::cpu::bsp_init();
        kernel::memory::vmm::init();
        verify_mmu();
    } else {
        kernel::print("WARNING: RAM window too small; PMM and MMU not initialized\n");
    }

    kernel::print("GIC: dist={}, cpu={}\n", reinterpret_cast<void*>(gic_dist), reinterpret_cast<void*>(gic_cpu));
    gic_init(gic_dist, gic_cpu);

    gic_distributor::set_priority(TIMER_IRQ, 0);
    gic_distributor::enable_irq(TIMER_IRQ);

    generic_timer::init_periodic(1000);

    // Prove the interrupt path end to end: the tick must actually reach the
    // handler, not merely be programmed.
    while (irq_count() < 5)
        asm volatile("wfi");

    kernel::scheduler::scheduler::init();
    verify_context_switch();
    syscall_init();

    init_rootfs();

    // A real thread so preemption has somewhere to go besides idle.
    kernel::scheduler::scheduler::spawn(&preempt_probe, 1);
    kernel::scheduler::scheduler::spawn(&user_hello, 2);

    smp_boot_aps(RUCUX_CPU_COUNT);

    kernel::print("rucux (aarch64) boot complete, {} CPUs online\n", kernel::cpu::g_cpu_count.load(kernel::relaxed));

    // Hand over: from here the timer tick drives scheduling, and this boot
    // context is abandoned on the next switch — exactly as ARMv7 does.
    enable_preemption();
    kernel::scheduler::scheduler::schedule();

    for (;;)
        asm volatile("wfi");
}

} // extern "C"
