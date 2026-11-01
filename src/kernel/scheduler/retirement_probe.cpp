// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/print.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/dispatch.hpp>
#include <kernel/scheduler/retirement_diagnostics.hpp>
#include <kernel/scheduler/retirement_probe.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>

namespace kernel::scheduler {
namespace {
atomic<uint32_t> coverage{0}, release_workers{0}, ready{0}, proceed{0};
atomic<uint32_t> identity_errors{0};
uint32_t parent_tid;
constexpr uint32_t BASE = 20000;
constexpr unsigned ROUNDS = RUCUX_RETIREMENT_ROUNDS;
constexpr uint32_t SEED = RUCUX_RETIREMENT_SEED;
uint32_t actor_seed() noexcept {
    return (SEED ^ (scheduler::current_thread()->tid * 2654435761U)) | 1U;
}
void checkpoint(uint32_t& state, unsigned point) noexcept {
    if constexpr (SEED == 0) return;
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    RETIREMENT_NOTE(phase, scheduler::current_thread()->tid, point, state);
    for (unsigned n = 0; n < (state & 3U); ++n)
        scheduler::yield();
}
uint32_t server_tid;
unsigned mode;
long result, cancelled_result;
ipc::fast_msg words;

// Yield counts are not time: another QEMU vCPU can be descheduled while this
// one executes an arbitrary number of same-thread scheduler passes.
uint64_t deadline() noexcept {
    return time_manager::get_ticks() + 5000;
}
bool expired(uint64_t limit) noexcept {
    return time_manager::get_ticks() >= limit;
}

void child_init() noexcept {
    auto* self = scheduler::current_thread();
    self->parent_tid = parent_tid;
    coverage.fetch_or(1U << cpu::this_cpu()->cpu_id, relaxed);
}
bool collect(uint32_t tid, int expected) noexcept {
    int status = -1;
    RETIREMENT_NOTE(collect_begin, tid, expected, 0);
    const auto observed = scheduler::sys_waitpid(static_cast<int>(tid), &status, 0);
    RETIREMENT_NOTE(collect_result, tid, observed, status);
    if (observed != tid || status != expected) {
        kernel::print("SMP retirement: wait tid {} got {} status {} expected {}\n", tid, observed, status, expected);
        return false;
    }
    // waitpid may consume status while the child still owns its outgoing stack.
    const auto limit = deadline();
    do {
        scheduler::cleanup_terminated();
        if (!scheduler::get_thread_by_tid(tid)) return true;
        scheduler::yield();
    } while (!expired(limit));
    return false;
}
void coverage_worker() noexcept {
    child_init();
    const auto flags = irq_save();
    auto* expected = cpu::this_cpu()->current_thread;
    irq_restore(flags);
    // The expected identity lives on this stack and must survive migration.
    // Hosted IRQ helpers cannot exercise the CPU-base/read preemption window.
    for (unsigned n = 0; n < 2048; ++n) {
        if (scheduler::current_thread() != expected) identity_errors.fetch_add(1, relaxed);
        coverage.fetch_or(1U << cpu::this_cpu()->cpu_id, relaxed);
        scheduler::yield();
    }
    ready.fetch_add(1, release);
    while (!release_workers.load(acquire)) {
        coverage.fetch_or(1U << cpu::this_cpu()->cpu_id, relaxed);
        scheduler::yield();
    }
    scheduler::exit(0);
}
void held_exit_worker() noexcept {
    child_init();
    auto* self = scheduler::current_thread();
    volatile uint64_t canary = 0xAABBCCDD11223344ULL;
    // Deliberately stretch the exit-publication window with IRQs masked. The
    // controller must run on another CPU, consume status and attempt cleanup.
    const auto flags = irq_save();
    self->exit_code = 42 << 8;
    self->exited = true;
    self->state.store(thread_state::TERMINATED, release);
    ready.store(1, release);
    while (!proceed.load(acquire))
        cpu_relax();
    if (canary != 0xAABBCCDD11223344ULL) ready.store(2, release);
    irq_restore(flags);
    scheduler::finish_termination();
}
void stop_if_cancelled() noexcept {
    if (process::signal_manager::consume_fatal_signal(scheduler::current_thread())) scheduler::finish_termination();
}
void server_worker() noexcept {
    child_init();
    auto random = actor_seed();
    // Enable fatal delivery for these kernel probes. No user-mode entry or
    // marshalled syscall uses this marker; kernel scopes protect IPC/sleep.
    scheduler::current_thread()->user_entry = reinterpret_cast<void*>(1);
    {
        kernel_scope scope(scheduler::current_thread());
        if (mode == 0) {
            ready.store(1, release);
            scheduler::sleep_until(kernel::time_manager::get_ticks() + 60000);
        } else {
            ipc::fast_msg request{};
            checkpoint(random, 10);
            const auto accepted = ipc::sys_ipc_wait(&request);
            RETIREMENT_NOTE(server_wait, scheduler::current_thread()->tid, accepted, request.d0);
            if (accepted != 0) scheduler::exit(1);
            ready.store(1, release);
            while (!proceed.load(acquire))
                scheduler::yield();
            if (mode == 1) scheduler::exit(0); // Exit while a caller is accepted.
            checkpoint(random, 11);
            const auto cancelled = ipc::sys_ipc_wait(&request);
            RETIREMENT_NOTE(cancelled_wait, scheduler::current_thread()->tid, cancelled, 0);
            checkpoint(random, 12);
            const auto ack = ipc::sys_ipc_reply(&request);
            RETIREMENT_NOTE(cancel_ack, scheduler::current_thread()->tid, ack, 0);
            checkpoint(random, 13);
            const auto next = ipc::sys_ipc_wait(&request);
            RETIREMENT_NOTE(server_next, scheduler::current_thread()->tid, next, request.d0);
            if (cancelled != -125 || ack != -125 || next != 0 || request.d0 != 77) scheduler::exit(2);
            request.d0 = 88;
            checkpoint(random, 14);
            const auto replied = ipc::sys_ipc_reply(&request);
            RETIREMENT_NOTE(server_reply, scheduler::current_thread()->tid, replied, request.d0);
            if (replied != 0) scheduler::exit(3);
        }
    }
    stop_if_cancelled();
    scheduler::exit(0);
}
void caller_worker() noexcept {
    child_init();
    auto random = actor_seed();
    scheduler::current_thread()->user_entry = reinterpret_cast<void*>(1);
    long status;
    {
        kernel_scope scope(scheduler::current_thread());
        checkpoint(random, 20);
        status = ipc::sys_ipc_call(server_tid, &words);
        RETIREMENT_NOTE(call_result, scheduler::current_thread()->tid, status, words.d0);
        checkpoint(random, 21);
    }
    stop_if_cancelled();
    result = status;
    scheduler::exit(0);
}
void cancelled_worker() noexcept {
    child_init();
    auto random = actor_seed();
    scheduler::current_thread()->user_entry = reinterpret_cast<void*>(1);
    ipc::fast_msg request{12, 11, 22, 33};
    long status;
    {
        kernel_scope scope(scheduler::current_thread());
        checkpoint(random, 30);
        status = ipc::sys_ipc_call(server_tid, &request);
        RETIREMENT_NOTE(cancel_result, scheduler::current_thread()->tid, status, request.d0);
        checkpoint(random, 31);
    }
    stop_if_cancelled();
    cancelled_result = status; // A killed syscall must never reach this point.
    scheduler::exit(1);
}
bool blocked(uint32_t tid) noexcept {
    const auto limit = deadline();
    do {
        auto* owner = scheduler::pin_thread(tid);
        if (!owner) return false;
        const bool asleep = owner->state.load(acquire) == thread_state::BLOCKED;
        owner->unpin();
        if (asleep) return true;
        scheduler::yield();
    } while (!expired(limit));
    return false;
}
bool wait_ready() noexcept {
    const auto limit = deadline();
    do {
        if (ready.load(acquire)) return true;
        scheduler::yield();
    } while (!expired(limit));
    return false;
}
} // namespace

bool verify_smp_retirement() noexcept {
    const auto fail = [](unsigned line) -> bool {
        kernel::print("SMP retirement: check {} mode {} result {} cancelled {} word {}\n", line, mode, result,
                      cancelled_result, words.d0);
        retirement::dump();
        (void)irq_save(); // Preserve the controller/children for the host's stopped-state capture.
        for (;;)
            cpu_relax();
    };
    kernel::print("retirement config: seed {} rounds {} version 1\n", SEED, ROUNDS);
    auto random = actor_seed();
    const uint32_t cpus = cpu::g_cpu_count.load(acquire);
    if (cpus < 2) {
        kernel::print("SMP retirement: SKIP (requires multiple CPUs)\n");
        return true;
    }
    if (cpus > 4) return fail(__LINE__);
    parent_tid = scheduler::current_thread()->process_id;
    for (unsigned i = 0; i < 8; ++i)
        scheduler::spawn(coverage_worker, BASE + i);
    const uint32_t expected = (1U << cpus) - 1;
    const auto limit = deadline();
    while (coverage.load(acquire) != expected || ready.load(acquire) != 8) {
        // The controller also executes the probe. Local affinity can keep it
        // and the preemption probe on a CPU while workers circulate elsewhere.
        coverage.fetch_or(1U << cpu::this_cpu()->cpu_id, relaxed);
        scheduler::yield();
        if (expired(limit)) return fail(__LINE__);
    }
    if (identity_errors.load(acquire)) return fail(__LINE__);
    kernel::print("SMP retirement: thread identity verified\n");
    release_workers.store(1, release);
    for (unsigned i = 0; i < 8; ++i)
        if (!collect(BASE + i, 0)) return fail(__LINE__);

    ready.store(0, release);
    scheduler::spawn(held_exit_worker, BASE + 8);
    if (!wait_ready()) return fail(__LINE__);
    int status = 0;
    const auto waited = scheduler::sys_waitpid(BASE + 8, &status, 0);
    scheduler::cleanup_terminated();
    const bool retained = scheduler::get_thread_by_tid(BASE + 8) != nullptr;
    const bool consumed_once = scheduler::sys_waitpid(BASE + 8, nullptr, 1) == -10;
    proceed.store(1, release); // Release even on failure so the remote CPU can stop.
    if (waited != BASE + 8 || status != (42 << 8) || !retained || !consumed_once) return fail(__LINE__);
    while (scheduler::get_thread_by_tid(BASE + 8)) {
        scheduler::cleanup_terminated();
        scheduler::yield();
    }
    if (ready.load(acquire) != 1) return fail(__LINE__);
    kernel::print("SMP retirement: held outgoing stack and single wait status verified\n");

    for (unsigned round = 0; round < ROUNDS; ++round) {
        checkpoint(random, 1);
        mode = round % 3;
        RETIREMENT_NOTE(round_start, scheduler::current_thread()->tid, round, mode);
        const uint32_t first = BASE + 16 + round * 3;
        server_tid = first;
        result = cancelled_result = -999;
        words = {12, 11, 22, 33};
        ready.store(0, release);
        proceed.store(0, release);
        scheduler::spawn(server_worker, first);
        if (mode == 0) {
            if (!wait_ready() || !blocked(first)) return fail(__LINE__);
            scheduler::spawn(caller_worker, first + 1);
            if (!blocked(first + 1) || scheduler::signal_thread(first, 9) != 0 || !collect(first, 9) ||
                !collect(first + 1, 0) || result != -32 || words.d0 != 11)
                return fail(__LINE__);
        } else if (mode == 1) {
            scheduler::spawn(caller_worker, first + 1);
            if (!wait_ready()) return fail(__LINE__);
            proceed.store(1, release);
            if (!collect(first, 0) || !collect(first + 1, 0) || result != -32 || words.d0 != 11) return fail(__LINE__);
        } else {
            scheduler::spawn(cancelled_worker, first + 1);
            if (!wait_ready() || scheduler::signal_thread(first + 1, 9) != 0 || !collect(first + 1, 9) ||
                cancelled_result != -999)
                return fail(__LINE__);
            checkpoint(random, 2);
            words.d0 = 77;
            scheduler::spawn(caller_worker, first + 2);
            if (!blocked(first + 2)) return fail(__LINE__);
            proceed.store(1, release);
            checkpoint(random, 3);
            if (!collect(first + 2, 0) || !collect(first, 0) || result != 0 || words.d0 != 88) return fail(__LINE__);
        }
        if (!dispatch::queues_valid()) return fail(__LINE__);
    }
    kernel::print("SMP retirement: PASS ({} CPUs, mask {}, {} IPC/exit/cancel/wait rounds)\n", cpus,
                  coverage.load(acquire), ROUNDS);
    return true;
}
} // namespace kernel::scheduler
