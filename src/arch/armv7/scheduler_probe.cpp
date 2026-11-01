// SPDX-License-Identifier: MIT
#include <arch/armv7/memory_probe.hpp>
#include <arch/armv7/timer.hpp>
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
namespace arch::armv7 {
namespace {
using namespace kernel;
using scheduler::scheduler;
atomic<uint32_t> coverage{0}, ready{0}, errors{0}, preempted{0};
uint32_t parent;
void worker() {
    auto* self = scheduler::current_thread();
    self->parent_tid = parent;
    volatile uint32_t canary = 0xAABB7733;
    ready.fetch_add(1, release);
    // A thread that never yields must still be interrupted and later resume.
    const auto flags = irq_save();
    const auto start_cpu = cpu::this_cpu()->cpu_id;
    const auto start_ticks = cpu::this_cpu()->ticks;
    irq_restore(flags);
    bool interrupted = false;
    const auto deadline = generic_timer::read_counter() + uint64_t{generic_timer::get_frequency()} * 5;
    while (!interrupted && generic_timer::read_counter() < deadline) {
        const auto saved = irq_save();
        auto* local = cpu::this_cpu();
        if (local->cpu_id != start_cpu || local->ticks != start_ticks) interrupted = true;
        if (scheduler::current_thread() != self || canary != 0xAABB7733) errors.fetch_add(1, relaxed);
        irq_restore(saved);
    }
    const auto final_flags = irq_save();
    interrupted = interrupted || cpu::this_cpu()->cpu_id != start_cpu || cpu::this_cpu()->ticks != start_ticks;
    irq_restore(final_flags);
    if (interrupted)
        preempted.fetch_add(1, release);
    else
        errors.fetch_add(1, relaxed);
    for (unsigned i = 0; i < 1024; ++i) {
        const auto flags = irq_save();
        coverage.fetch_or(1U << cpu::this_cpu()->cpu_id, relaxed);
        if ((flags & 31) != 0x13 || (flags & 0x80) || scheduler::current_thread() != self || canary != 0xAABB7733)
            errors.fetch_add(1, relaxed);
        irq_restore(flags);
        scheduler::yield();
    }
    scheduler::exit(0);
}
} // namespace
void scheduler_probe() noexcept {
    parent = scheduler::current_thread()->process_id;
    for (unsigned i = 0; i < 8; ++i)
        scheduler::spawn(worker, 20000 + i);
    while (ready.load(acquire) != 8)
        scheduler::yield();
    bool ok = true;
    for (unsigned i = 0; i < 8; ++i) {
        int status = -1;
        if (scheduler::sys_waitpid(20000 + i, &status, 0) != static_cast<long>(20000 + i) || status) ok = false;
    }
    const auto expected = (1U << cpu::g_cpu_count.load(acquire)) - 1;
    ok = ok && !errors.load(acquire) && coverage.load(acquire) == expected && preempted.load(acquire) == 8;
    print("ARMv7 scheduler: timer preemption {} ({} workers)\n", preempted.load(acquire) == 8 ? "PASS" : "FAIL",
          preempted.load(acquire));
    print("ARMv7 scheduler: {} (CPU mask {}, 8 workers, 8192 yields)\n", ok ? "PASS" : "FAIL", coverage.load(acquire));
    scheduler::exit(ok ? 0 : 1);
}
} // namespace arch::armv7
