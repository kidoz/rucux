// SPDX-License-Identifier: MIT
#include <arch/amd64/apic.hpp>
#include <arch/amd64/gdt.hpp>
#include <arch/amd64/idt.hpp>
#include <arch/amd64/pit.hpp>
#include <arch/amd64/smp.hpp>
#include <arch/amd64/syscall.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace arch::amd64 {
extern "C" {
extern uint8_t ap_trampoline_start[], ap_trampoline_end[];
extern uint32_t ap_pml4[], ap_cpu[];
extern uint64_t ap_stack[], ap_entry[];
}
namespace {
constexpr uintptr_t TRAMPOLINE_PHYS = 0x8000;
kernel::atomic<uint32_t> ready{0};
kernel::atomic<bool> released{false};
uint32_t timer_count;
uintptr_t field(const void* symbol) {
    return TRAMPOLINE_PHYS + reinterpret_cast<uintptr_t>(symbol) - reinterpret_cast<uintptr_t>(ap_trampoline_start);
}
bool await(uint32_t count) {
    for (unsigned interval = 0; interval < 50; ++interval) {
        if (ready.load(kernel::acquire) == count) return true;
        if (!pit::wait_10ms()) return false;
    }
    return false;
}
} // namespace
extern "C" void ap_main(uint32_t id) {
    gdt_init(id);
    kernel::cpu::ap_init(id, lapic::id());
    idt_load();
    syscall_init();
    lapic::init_cpu(); // Never overwrite the BSP's shared MMIO base.
    kernel::memory::vmm::switch_to(kernel::memory::vmm::get_active_page_table());
    kernel::scheduler::scheduler::init();
    lapic::timer_init(0x20, timer_count, 0x03);
    kernel::print("AP{}: ready (APIC ID {})\n", id, lapic::id());
    ready.fetch_add(1, kernel::release); // IDT, IST, GS and NMI service are ready.
    while (!released.load(kernel::acquire)) {
#ifdef RUCUX_AMD64_SMP_SELF_TEST
        kernel::memory::vmm::probe_tlb_on_ap(id);
#endif
        kernel::cpu_relax();
    }
    kernel::scheduler::scheduler::schedule();
    for (;;)
        asm volatile("sti; hlt");
}

bool smp_boot_aps(const acpi::madt_info& info, uintptr_t root, uintptr_t reserved_page, uint32_t count) noexcept {
    timer_count = count;
    kernel::cpu::get_cpu(0)->apic_id = lapic::id();
    unsigned enabled = 0;
    for (unsigned i = 0; i < info.cpu_count; ++i)
        if (info.cpus[i].enabled && info.cpus[i].apic_id != lapic::id()) ++enabled;
    if (!enabled) {
        kernel::print("SMP: 0 secondary cores ready\n");
        return true;
    }
    const auto size = static_cast<size_t>(ap_trampoline_end - ap_trampoline_start);
    if (reserved_page != TRAMPOLINE_PHYS || size > 4096 || (root & 4095) || root > 0xFFFFF000ULL) return false;
    lib::memcpy(reinterpret_cast<void*>(reserved_page), ap_trampoline_start, size);
    *reinterpret_cast<uint32_t*>(field(ap_pml4)) = static_cast<uint32_t>(root);
    *reinterpret_cast<uint64_t*>(field(ap_entry)) = reinterpret_cast<uintptr_t>(ap_main);
    for (unsigned i = 0; i < info.cpu_count; ++i) {
        if (!info.cpus[i].enabled || info.cpus[i].apic_id == lapic::id()) continue;
        const uint32_t id = ready.load(kernel::acquire) + 1;
        if (id >= kernel::cpu::MAX_CPUS) return false;
        auto* stack = new uint8_t[32768]; // Boot stack remains owned for the CPU's lifetime.
        if (!stack) return false;
        *reinterpret_cast<uint64_t*>(field(ap_stack)) = reinterpret_cast<uintptr_t>(stack + 32768);
        *reinterpret_cast<uint32_t*>(field(ap_cpu)) = id;
        // Publish all launch fields before issuing the startup IPIs.
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (!lapic::send_init(info.cpus[i].apic_id)) return false;
        lapic::send_sipi(info.cpus[i].apic_id, TRAMPOLINE_PHYS / 4096);
        if (!await(id)) {
            lapic::send_sipi(info.cpus[i].apic_id, TRAMPOLINE_PHYS / 4096);
            if (!await(id)) return false; // Never reuse fields after a late/timed-out AP.
        }
    }
    kernel::print("SMP: {} secondary cores ready\n", ready.load(kernel::acquire));
    return ready.load(kernel::acquire) == enabled;
}
void smp_release_aps() noexcept {
    released.store(true, kernel::release);
}
} // namespace arch::amd64
