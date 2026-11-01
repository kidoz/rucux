// SPDX-License-Identifier: MIT
#include <arch/armv7/memory_probe.hpp>
#include <arch/armv7/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/tlb.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
namespace arch::armv7 {
namespace {
using namespace kernel;
using namespace kernel::memory;
constexpr uintptr_t ALIAS = USER_BEGIN + 0x200000;
constexpr uint32_t ROUNDS = 64, DONE = 1000;
atomic<uint32_t> root{0}, command{0}, joined{1}, left{0}, bad{0}, acknowledgements[8]{};
uint32_t stamp(unsigned round, bool second) {
    return (second ? 0x22B00000U : 0x11A00000U) | round;
}
uint32_t translation(bool write) {
    uint32_t par;
    if (write)
        asm volatile("mcr p15, 0, %0, c7, c8, 1" ::"r"(ALIAS) : "memory");
    else
        asm volatile("mcr p15, 0, %0, c7, c8, 0" ::"r"(ALIAS) : "memory");
    asm volatile("isb; mrc p15, 0, %0, c7, c4, 0" : "=r"(par)::"memory");
    return par;
}
void check(uint32_t id, uint32_t phase) {
    uint32_t cpsr;
    asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    const unsigned operation = (phase - 1) % 4, round = (phase - 1) / 4;
    bool ok = (cpsr & 0x80) && (cpsr & 31) == 0x13;
    uint32_t value = 0, par = 0;
    if (operation < 3) {
        value = *reinterpret_cast<volatile uint32_t*>(ALIAS);
        ok = ok && value == stamp(round, operation == 2);
        par = translation(true);
        ok = ok && ((par & 1) != 0) == (operation == 1);
        ok = ok && !(translation(false) & 1);
    } else {
        par = translation(false);
        ok = ok && (par & 1);
    }
    if (!ok) {
        bad.fetch_or(1U << id, relaxed);
        print("ARMv7 TLB mismatch: cpu {} phase {} value {} PAR {} CPSR {}\n", id, phase, value, par, cpsr);
    }
    acknowledgements[id].store(phase, release);
}
bool await(atomic<uint32_t>& word, uint32_t expected) {
    const auto deadline = generic_timer::read_counter() + uint64_t{generic_timer::get_frequency()} * 5;
    while (word.load(acquire) != expected) {
        if (generic_timer::read_counter() >= deadline) return false;
        cpu_relax();
    }
    return true;
}
bool phase(uint32_t number, unsigned count) {
    command.store(number, release);
    check(0, number);
    for (unsigned id = 1; id < count; ++id)
        if (!await(acknowledgements[id], number)) return false;
    return !bad.load(acquire);
}
} // namespace
void memory_probe_on_ap(uint32_t id) noexcept {
    const auto shared = root.load(acquire);
    if (!shared) return;
    const auto original = vmm::get_active_page_table();
    vmm::switch_to(shared);
    joined.fetch_or(1U << id, release);
    uint32_t previous = 0;
    for (;;) {
        const auto next = command.load(acquire);
        if (next == DONE) break;
        if (next && next != previous) {
            check(id, next);
            previous = next;
        }
        cpu_relax();
    }
    vmm::switch_to(original);
    left.fetch_or(1U << id, release);
    // Do not re-enter the retired root from the AP startup polling loop.
    while (root.load(acquire))
        cpu_relax();
}
bool verify_broadcast_memory() noexcept {
    const auto count = cpu::g_cpu_count.load(acquire), expected = (1U << count) - 1;
    const auto original = vmm::get_active_page_table(), shared = vmm::create_address_space();
    auto* first = static_cast<uint32_t*>(pmm::alloc_page());
    auto* second = static_cast<uint32_t*>(pmm::alloc_page());
    if (!shared || !first || !second) return false;
    root.store(shared, release);
    vmm::switch_to(shared);
    if (!await(joined, expected)) return false;
    {
        irq_lock_guard guard(user_mapping_lock);
        if (tlb::active.mask(shared) != expected) return false;
    }
    const auto rw = page_flags::PRESENT | page_flags::WRITABLE | page_flags::NO_EXECUTE;
    const auto ro = page_flags::PRESENT | page_flags::NO_EXECUTE;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        *first = stamp(round, false);
        *second = stamp(round, true);
        vmm::map(ALIAS, reinterpret_cast<uintptr_t>(first), rw);
        if (!phase(round * 4 + 1, count) || !vmm::protect(ALIAS, ro) || !phase(round * 4 + 2, count)) return false;
        if (!vmm::protect(ALIAS, rw)) return false;
        vmm::map(ALIAS, reinterpret_cast<uintptr_t>(second), rw);
        auto* previous = first;
        pmm::free_page(first);
        first = static_cast<uint32_t*>(pmm::alloc_page());
        if (first != previous) return false;
        *first = 0xDEAD0000 | round;
        if (!phase(round * 4 + 3, count)) return false;
        vmm::unmap(ALIAS);
        previous = second;
        pmm::free_page(second);
        second = static_cast<uint32_t*>(pmm::alloc_page());
        if (second != previous) return false;
        *second = 0xBEEF0000 | round;
        if (!phase(round * 4 + 4, count)) return false;
    }
    command.store(DONE, release);
    vmm::switch_to(original);
    if (!await(left, expected & ~1U) || vmm::is_active(shared)) return false;
    vmm::discard_address_space(shared);
    pmm::free_page(first);
    pmm::free_page(second);
    root.store(0, release);
    print("ARMv7 TLB: PASS ({} CPUs, mask {}, {} replace/protect/unmap/reuse rounds)\n", count, joined.load(acquire),
          ROUNDS);
    print("ARMv7 TLB: inactive root teardown PASS\n");
    if (count == 1) print("ARMv7 TLB: broadcast SKIP (one CPU)\n");
    return true;
}
} // namespace arch::armv7
