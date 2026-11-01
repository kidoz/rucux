// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/retirement_diagnostics.hpp>
#include <kernel/scheduler/scheduler.hpp>

namespace kernel::scheduler::retirement {
namespace {
constexpr uint32_t CAPACITY = 64;
struct record {
    uint32_t sequence, kind, tid;
    uint64_t a, b;
};
struct buffer {
    irq_spinlock lock;
    uint32_t written = 0;
    record records[CAPACITY]{};
};
buffer buffers[cpu::MAX_CPUS];
atomic<uint32_t> sequence{0};
atomic<bool> frozen{false};
} // namespace
void note(event kind, uint32_t tid, uint64_t a, uint64_t b) noexcept {
    if (frozen.load(relaxed)) return;
    const auto flags = irq_save();
    auto& local = buffers[cpu::this_cpu()->cpu_id];
    {
        irq_lock_guard guard(local.lock);
        if (!frozen.load(acquire)) {
            local.records[local.written++ % CAPACITY] = {sequence.fetch_add(1, relaxed), kind, tid, a, b};
        }
    }
    irq_restore(flags);
}
void dump() noexcept {
    frozen.store(true, release);
    kernel::print("retirement diagnostics: begin\n");
    scheduler::dump_retirement_state();
    for (uint32_t id = 0; id < cpu::g_cpu_count.load(acquire); ++id) {
        record copy[CAPACITY];
        uint32_t written, count;
        {
            irq_lock_guard guard(buffers[id].lock);
            written = buffers[id].written;
            count = written < CAPACITY ? written : CAPACITY;
            for (uint32_t n = 0; n < count; ++n)
                copy[n] = buffers[id].records[(written - count + n) % CAPACITY];
        }
        kernel::print("retirement buffer: cpu {} written {} retained {}\n", id, written, count);
        for (uint32_t n = 0; n < count; ++n) {
            const auto& r = copy[n];
            kernel::print("retirement event: cpu {} seq {} kind {} tid {} a {} b {}\n", id, r.sequence, r.kind, r.tid,
                          r.a, r.b);
        }
    }
    kernel::print("retirement diagnostics: end\n");
}
} // namespace kernel::scheduler::retirement
