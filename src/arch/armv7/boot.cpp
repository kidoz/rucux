// SPDX-License-Identifier: MIT
#include <arch/armv7/console.hpp>
#include <arch/armv7/exception.hpp>
#include <arch/armv7/gic.hpp>
#include <arch/armv7/psci.hpp>
#include <arch/armv7/smp.hpp>
#include <arch/armv7/timer.hpp>
#include <arch/armv7/uart.hpp>
#include <arch/armv7/usb.hpp>
#include <arch/armv7/dwmac.hpp>
#include <arch/armv7/mali450.hpp>
#include <kernel/boot_protocol.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/net/netif.hpp>
#include <kernel/net/socket.hpp>
#include <lib/type_traits.hpp>
#include <stdint.h>

// Verification of type_traits
static_assert(lib::is_same_v<lib::int32_t, int>);
static_assert(lib::is_same_v<lib::uint32_t, unsigned int>);
static_assert(lib::is_same_v<lib::remove_reference_t<int&>, int>);
static_assert(lib::is_same_v<lib::remove_cv_t<const volatile int>, int>);

extern "C" {

// Default GIC addresses for Odroid C2 / Amlogic S905 (Cortex-A53)
// These would normally come from the device tree.
static constexpr uintptr_t GIC_DIST_BASE = 0xC4301000;
static constexpr uintptr_t GIC_CPU_BASE  = 0xC4302000;

// Number of CPUs (Odroid C2 has 4 Cortex-A53 cores)
static constexpr uint32_t NUM_CPUS = 4;

void kernel_main(rucux_boot_info* info) {
    arch::armv7::uart::init();
    arch::armv7::console::init_early();
    arch::armv7::exceptions_init();

    kernel::print("rucux (armv7) Initialized!\n");
    kernel::print("Magic: {}, Info: {}\n",
                  reinterpret_cast<void*>(info ? info->magic : 0),
                  reinterpret_cast<void*>(info));

    // Initialize PMM from boot info memory map
    if (info && info->magic == RUCUX_BOOT_MAGIC) {
        kernel::memory::pmm::memory_map_entry entries[64];
        size_t count = 0;
        size_t mmap_entries = info->mmap_size / info->mmap_descriptor_size;

        for (size_t i = 0; i < mmap_entries && count < 64; ++i) {
            rucux_mmap_entry* mmap = reinterpret_cast<rucux_mmap_entry*>(
                reinterpret_cast<uint8_t*>(info->mmap) + (i * info->mmap_descriptor_size));
            entries[count].base = mmap->physical_start;
            entries[count].length = mmap->number_of_pages * 4096;
            entries[count].type = mmap->type;
            count++;
        }
        kernel::memory::pmm::init(entries, count);
        kernel::print("PMM: {} MB free\n",
                      (kernel::memory::pmm::get_free_pages() * 4096) / (1024 * 1024));
    } else {
        kernel::print("WARNING: No valid boot info found! Using hardcoded Odroid C2 memory map.\n");
        // Odroid C2 has 2GB of RAM starting at 0x00000000.
        // We start our usable pool at 0x11000000 to safely skip ROM, ATF, U-Boot, and the kernel itself.
        kernel::memory::pmm::memory_map_entry entries[1];
        entries[0].base = 0x11000000;
        entries[0].length = 0x6E000000; // ~1760 MB (up to 0x7F000000)
        entries[0].type = 7; // EfiConventionalMemory
        kernel::memory::pmm::init(entries, 1);
        kernel::print("PMM fallback: {} MB free\n",
                      (kernel::memory::pmm::get_free_pages() * 4096) / (1024 * 1024));
    }

    kernel::memory::vmm::init();

    // Initialize per-CPU for BSP
    kernel::cpu::bsp_init();

    // Initialize GIC
    arch::armv7::gic_init(GIC_DIST_BASE, GIC_CPU_BASE);

    // Enable virtual timer IRQ (PPI 27)
    arch::armv7::gic_distributor::enable_irq(27);
    arch::armv7::gic_distributor::set_priority(27, 0);

    // Initialize per-CPU timer (~1000 Hz)
    arch::armv7::generic_timer::init_periodic(1000);

    // Initialize scheduler
    kernel::scheduler::scheduler::init();
    
    // Initialize network stack and Odroid C2 Ethernet driver
    kernel::net::net_init();
    kernel::net::socket_manager::init();
    arch::armv7::dwmac::init();

    // Initialize Mali-450 GPU
    arch::armv7::mali450::init();

    // Initialize USB & Hub
    arch::armv7::usb::init();

    // Boot secondary cores via PSCI
    arch::armv7::smp_boot_aps(NUM_CPUS);

    kernel::print("rucux (armv7) boot complete, {} CPUs online\n",
                  kernel::cpu::g_cpu_count.load(kernel::relaxed));

    // Enable interrupts and enter scheduler
    asm volatile("cpsie i");
    kernel::scheduler::scheduler::schedule();

    while (true) {
        asm volatile("wfi");
    }
}

} // extern "C"
