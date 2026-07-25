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
#include <arch/armv7/hw_rng.hpp>
#include <arch/armv7/watchdog.hpp>
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
#include <kernel/fdt.hpp>

// Verification of type_traits
static_assert(lib::is_same_v<lib::int32_t, int>);
static_assert(lib::is_same_v<lib::uint32_t, unsigned int>);
static_assert(lib::is_same_v<lib::remove_reference_t<int&>, int>);
static_assert(lib::is_same_v<lib::remove_cv_t<const volatile int>, int>);

extern "C" {

static constexpr uintptr_t GIC_DIST_BASE = 0xC4301000;
static constexpr uintptr_t GIC_CPU_BASE  = 0xC4302000;
static constexpr uint32_t NUM_CPUS = 4;

void kernel_main(uint32_t r0, uint32_t r1, uint32_t r2) {
    (void)r1;
    
    arch::armv7::exceptions_init();

    bool has_fdt = false;
    uint32_t mem_base = 0;
    uint32_t mem_size = 0;
    
    if (r2 != 0 && kernel::fdt::init(reinterpret_cast<void*>(r2))) {
        has_fdt = true;
        uintptr_t uart_base = 0;
        bool is_pl011 = false;
        if (kernel::fdt::get_uart(&uart_base, &is_pl011)) {
            arch::armv7::uart::init_dynamic(uart_base, is_pl011);
        } else {
            arch::armv7::uart::init();
        }
    } else {
        arch::armv7::uart::init();
    }
    
    arch::armv7::console::init_early();

    kernel::print("Early Boot: r0={}, r1={}, r2={}\n",
                  reinterpret_cast<void*>(r0),
                  reinterpret_cast<void*>(r1),
                  reinterpret_cast<void*>(r2));

    kernel::print("rucux (armv7) Initialized!\n");

    if (has_fdt) {
        kernel::print("FDT parsed at {}\n", reinterpret_cast<void*>(r2));
        
        uint64_t m_base, m_size;
        if (kernel::fdt::get_memory(&m_base, &m_size)) {
            mem_base = static_cast<uint32_t>(m_base);
            mem_size = static_cast<uint32_t>(m_size);
            kernel::print("FDT Memory: base={}, size={}\n", 
                reinterpret_cast<void*>(static_cast<uintptr_t>(mem_base)), 
                reinterpret_cast<void*>(static_cast<uintptr_t>(mem_size)));
            kernel::memory::pmm::memory_map_entry entries[1];
            // Skip first 16MB for kernel code/data and FDT
            entries[0].base = mem_base + 0x1000000;
            entries[0].length = (mem_size > 0x1000000) ? (mem_size - 0x1000000) : 0;
            entries[0].type = 1;
            kernel::memory::pmm::init(entries, 1);
            kernel::print("PMM from FDT: {} MB free\n", (kernel::memory::pmm::get_free_pages() * 4096) / (1024 * 1024));
        } else {
            kernel::print("WARNING: No memory node in FDT! Cannot init PMM.\n");
        }
    } else {
        kernel::print("WARNING: No valid boot info or FDT found! Using hardcoded Odroid C2 memory map.\n");
        kernel::memory::pmm::memory_map_entry entries[1];
        entries[0].base = 0x11000000;
        entries[0].length = 0x6E000000; // ~1760 MB (up to 0x7F000000)
        entries[0].type = 1; 
        kernel::memory::pmm::init(entries, 1);
        kernel::print("PMM fallback: {} MB free\n",
                      (kernel::memory::pmm::get_free_pages() * 4096) / (1024 * 1024));
    }

    kernel::memory::vmm::init();

    kernel::cpu::bsp_init();

    uintptr_t gic_dist = GIC_DIST_BASE;
    uintptr_t gic_cpu = GIC_CPU_BASE;
    if (has_fdt) {
        if (!kernel::fdt::get_gic(&gic_dist, &gic_cpu)) {
            kernel::print("WARNING: FDT get_gic failed, using hardcoded!\n");
        }
    }
    kernel::print("GIC: dist={}, cpu={}\n", 
                  reinterpret_cast<void*>(gic_dist), 
                  reinterpret_cast<void*>(gic_cpu));
    arch::armv7::gic_init(gic_dist, gic_cpu);

    arch::armv7::gic_distributor::enable_irq(27);
    arch::armv7::gic_distributor::set_priority(27, 0);

    arch::armv7::generic_timer::init_periodic(1000);

    kernel::scheduler::scheduler::init();
    
    kernel::net::net_init();
    kernel::net::socket_manager::init();
    
    auto devices = kernel::fdt::get_device_info();

    if (!has_fdt || devices.has_dwmac) {
        arch::armv7::dwmac::init();
    }
    if (!has_fdt || devices.has_mali450) {
        arch::armv7::mali450::init();
    }
    if (!has_fdt || devices.has_usb) {
        arch::armv7::usb::init();
    }
    if (!has_fdt || devices.has_hw_rng) {
        arch::armv7::hw_rng::init();
    }
    if (!has_fdt || devices.has_watchdog) {
        arch::armv7::watchdog::init(5000);
    }

    uint32_t num_cpus = has_fdt ? 1 : NUM_CPUS;
    arch::armv7::smp_boot_aps(num_cpus);

    kernel::print("rucux (armv7) boot complete, {} CPUs online\n",
                  kernel::cpu::g_cpu_count.load(kernel::relaxed));

    asm volatile("cpsie i");
    kernel::scheduler::scheduler::schedule();

    while (true) {
        asm volatile("wfi");
    }
}

} // extern "C"
