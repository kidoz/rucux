// SPDX-License-Identifier: MIT
#include <arch/armv7/console.hpp>
#include <arch/armv7/dwmac.hpp>
#include <arch/armv7/exception.hpp>
#include <arch/armv7/gic.hpp>
#include <arch/armv7/hw_rng.hpp>
#include <arch/armv7/mali450.hpp>
#include <arch/armv7/memory_probe.hpp>
#include <arch/armv7/smp.hpp>
#include <arch/armv7/timer.hpp>
#include <arch/armv7/topology.hpp>
#include <arch/armv7/uart.hpp>
#include <arch/armv7/usb.hpp>
#include <arch/armv7/watchdog.hpp>
#include <generated_arm_platform.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/fdt.hpp>
#include <kernel/memory/heap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/slab.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/net/netif.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
namespace {
[[noreturn]] void fail(const char* reason) {
    kernel::print("ARMv7 boot: FAIL ({})\n", reason);
    for (;;)
        asm volatile("cpsid if; wfi");
}
} // namespace
extern "C" uint8_t kernel_end[];
extern "C" void kernel_main(uint32_t, uint32_t, uint32_t fdt_address) {
    using namespace arch::armv7;
    init_exception_mode_stacks(0);
    exceptions_init();
    uart::init_dynamic(platform::UART_BASE, platform::UART_BASE == 0x09000000);
    console::init_early();
    kernel::cpu::bsp_init();
    kernel::print("rucux (armv7) Initialized!\n");
    cpu_topology topology{};
    bool has_fdt = false;
    if (fdt_address) {
        if (!parse_cpu_topology(reinterpret_cast<void*>(fdt_address), 0x100000, topology)) fail("invalid FDT topology");
        has_fdt = kernel::fdt::init(reinterpret_cast<void*>(fdt_address));
    } else {
        topology.count = platform::CPU_COUNT;
        topology.psci = true;
        topology.hvc = platform::PSCI_CONDUIT != 0;
        if (topology.count > MAX_CPUS) fail("manifest CPU count");
        for (unsigned id = 0; id < topology.count; ++id)
            topology.mpidrs[id] = id;
    }
    uintptr_t uart_base = platform::UART_BASE;
    bool pl011 = uart_base == 0x09000000;
    if (has_fdt && kernel::fdt::get_uart(&uart_base, &pl011)) uart::init_dynamic(uart_base, pl011);
    uint64_t base = platform::RAM_BASE, size = platform::RAM_SIZE;
    if (has_fdt && !kernel::fdt::get_memory(&base, &size)) fail("missing RAM");
    if (base > UINT32_MAX || size <= 0x1000000 || size > UINT32_MAX || base + size > 0x100000000ULL) fail("RAM range");
    if (reinterpret_cast<uintptr_t>(kernel_end) > base + 0x1000000) fail("kernel reservation");
    if (has_fdt && (fdt_address < base || uint64_t{fdt_address} + topology.blob_size > base + size))
        fail("FDT outside RAM");
    // Keep the kernel/boot stacks and the actual FDT out of allocator ranges.
    kernel::memory::pmm::memory_map_entry entries[2]{};
    uint64_t begin = base + 0x1000000, end = base + size;
    uint64_t dtb_begin = fdt_address & ~uint64_t{4095};
    uint64_t dtb_end = (uint64_t{fdt_address} + topology.blob_size + 4095) & ~uint64_t{4095};
    size_t count = 0;
    if (has_fdt && dtb_begin < end && dtb_end > begin) {
        if (dtb_begin > begin)
            entries[count++] = {static_cast<uintptr_t>(begin), static_cast<size_t>(dtb_begin - begin), 1};
        if (dtb_end < end) entries[count++] = {static_cast<uintptr_t>(dtb_end), static_cast<size_t>(end - dtb_end), 1};
    } else
        entries[count++] = {static_cast<uintptr_t>(begin), static_cast<size_t>(end - begin), 1};
    if (!count) fail("reserved RAM");
    kernel::memory::pmm::init(entries, count);
    kernel::memory::vmm::configure_ram(static_cast<uintptr_t>(base), static_cast<size_t>(size));
    kernel::memory::vmm::init();
    kernel::memory::vmm::switch_to(kernel::memory::vmm::get_active_page_table());
    kernel::memory::heap::init();
    kernel::memory::slab_init();
    kernel::scheduler::scheduler::init();
    kernel::time_manager::init();
    uintptr_t distributor = platform::GIC_DIST_BASE, interface = platform::GIC_CPU_BASE;
    if (has_fdt && !kernel::fdt::get_gic(&distributor, &interface)) fail("missing GIC");
    gic_init(distributor, interface);
    gic_distributor::set_priority(platform::TIMER_IRQ, 0);
    gic_distributor::enable_irq(platform::TIMER_IRQ);
    if (!generic_timer::init_periodic(1000)) fail("timer");
    kernel::net::net_init();
    kernel::net::socket_manager::init();
    const auto devices = kernel::fdt::get_device_info();
    if (!has_fdt || devices.has_dwmac) dwmac::init();
    if (!has_fdt || devices.has_mali450) mali450::init();
    if (!has_fdt || devices.has_usb) usb::init();
    if (!has_fdt || devices.has_hw_rng) hw_rng::init();
    if (!has_fdt || devices.has_watchdog) watchdog::init(5000);
    if (!smp_boot_aps(topology)) fail("AP startup");
#ifdef RUCUX_ARMV7_SMP_SELF_TEST
    if (!verify_broadcast_memory()) fail("broadcast TLB");
    kernel::scheduler::scheduler::spawn(scheduler_probe, 100);
#endif
    kernel::print("rucux (armv7) boot complete, {} CPUs online\n", kernel::cpu::g_cpu_count.load(kernel::acquire));
    smp_release_aps();
    kernel::scheduler::scheduler::schedule();
    for (;;)
        asm volatile("cpsie i; wfi");
}
