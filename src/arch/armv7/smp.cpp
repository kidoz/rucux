// SPDX-License-Identifier: MIT
#include <arch/armv7/exception.hpp>
#include <arch/armv7/gic.hpp>
#include <arch/armv7/memory_probe.hpp>
#include <arch/armv7/psci.hpp>
#include <arch/armv7/smp.hpp>
#include <arch/armv7/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
namespace arch::armv7 {
extern "C" void armv7_ap_entry();
namespace {
kernel::atomic<uint32_t> ready{0};
kernel::atomic<bool> released{false};
uint32_t affinity() {
    uint32_t value;
    asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(value));
    return value & 0xFFFFFF;
}
} // namespace
extern "C" void ap_entry_arm(uint32_t id) {
    init_exception_mode_stacks(id);
    exceptions_init();
    kernel::memory::vmm::enable_on_this_cpu();
    kernel::cpu::ap_init(id, affinity());
    kernel::memory::vmm::switch_to(kernel::memory::vmm::get_active_page_table());
    gic_cpu_interface::init_cpu();
    gic_distributor::set_priority(27, 0);
    gic_distributor::enable_irq(27);
    kernel::scheduler::scheduler::init();
    generic_timer::init_periodic(1000);
    kernel::print("AP{}: ready (MPIDR {})\n", id, affinity());
    ready.fetch_add(1, kernel::release);
    while (!released.load(kernel::acquire)) {
#ifdef RUCUX_ARMV7_SMP_SELF_TEST
        memory_probe_on_ap(id);
#endif
        kernel::cpu_relax();
    }
    kernel::scheduler::scheduler::schedule();
    for (;;)
        asm volatile("cpsie i; wfi");
}
bool smp_boot_aps(const cpu_topology& topology) noexcept {
    const auto self = affinity();
    kernel::cpu::get_cpu(0)->apic_id = self;
    bool found = false;
    for (uint32_t i = 0; i < topology.count; ++i)
        if (topology.mpidrs[i] == self) found = true;
    if (!found || !topology.count || topology.count > MAX_CPUS) return false;
    if (topology.count == 1) {
        kernel::print("SMP: Single CPU, skipping AP boot\n");
        return true;
    }
    psci::set_conduit(topology.hvc);
    const auto version = psci::version();
    if (!topology.psci || version < 2) return false;
    kernel::print("SMP: PSCI version {}, conduit {}\n", version, topology.hvc ? "hvc" : "smc");
    uint32_t id = 1;
    for (uint32_t n = 0; n < topology.count; ++n) {
        if (topology.mpidrs[n] == self) continue;
        const auto rc = psci::cpu_on(topology.mpidrs[n], reinterpret_cast<uintptr_t>(armv7_ap_entry), id);
        if (rc != psci_ret::SUCCESS) return false;
        const auto deadline = generic_timer::read_counter() + generic_timer::get_frequency();
        while (ready.load(kernel::acquire) < id) {
            if (generic_timer::read_counter() >= deadline) return false;
            kernel::cpu_relax();
        }
        ++id;
    }
    kernel::print("SMP: {} secondary cores ready\n", ready.load(kernel::acquire));
    return true;
}
void smp_release_aps() noexcept {
    released.store(true, kernel::release);
}
} // namespace arch::armv7
