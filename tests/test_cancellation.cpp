// SPDX-License-Identifier: MIT
// Production syscall marshalling, signal policy and futex cleanup; scheduler
// edges record readiness/termination rather than executing a context switch.
#include "test_harness.hpp"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/futex.hpp>
#include <kernel/syscall.hpp>
#include <kernel/vfs/vfs.hpp>
#include <new>
#include <thread>
#include <uapi/kernel/syscalls.h>

namespace {
using namespace kernel;
using process::signal_manager;
using scheduler::kernel_scope;
using scheduler::thread;
using scheduler::thread_state;
thread current{};
alignas(4096) uint8_t user_page[4096];
std::atomic<size_t> live_allocations{0};
size_t baseline;
unsigned resources, closed, stopped, queued, scheduled, depth_seen;
bool closed_with_resources, deferred, invoked;
void (*on_schedule)();
void (*on_translate)();
struct resource {
    resource() { ++resources; }
    ~resource() { --resources; }
};
void reset() {
    new (&current) thread{};
    current.tid = 7;
    current.user_entry = reinterpret_cast<void*>(1);
    current.pml4_phys = 123;
    current.state.store(thread_state::RUNNING, kernel::release);
    std::memset(user_page, 0xA5, sizeof(user_page));
    resources = closed = stopped = queued = scheduled = depth_seen = 0;
    deferred = closed_with_resources = invoked = false;
    on_schedule = on_translate = nullptr;
    sync::futex_init();
    baseline = live_allocations.load();
}
long interrupted_read(long, long, long, long, long, long, long) {
    resource held;
    depth_seen = current.kernel_depth.load();
    ASSERT_EQ(signal_manager::sys_kill(0, 15), 0);
    deferred = !signal_manager::consume_fatal_signal(&current);
    return 0;
}
long success(long, long, long buffer, long, long, long, long) {
    invoked = true;
    if (buffer) *reinterpret_cast<uint8_t*>(buffer) = 0x42;
    return 1;
}
long futex_handler(long, long address, long, long, long, long, long) {
    return sync::futex_wait(reinterpret_cast<uint32_t*>(address), 0xA5A5A5A5);
}
long read_call(syscall_handler handler) {
    return checked_syscall(handler, SYS_READ, 0, memory::USER_BEGIN, 16, 0, 0, 0);
}
void check_stopped() {
    ASSERT_EQ(resources, 0U);
    ASSERT_EQ(closed, 1U);
    ASSERT_EQ(stopped, 1U);
    ASSERT(!closed_with_resources);
    ASSERT_EQ(current.kernel_depth.load(), 0U);
    ASSERT_EQ(live_allocations.load(), baseline);
    ASSERT(current.state.load(kernel::acquire) == thread_state::TERMINATED);
}
} // namespace
void* operator new(size_t size) {
    void* result = std::malloc(size ? size : 1);
    if (!result) std::abort();
    live_allocations.fetch_add(1);
    return result;
}
void* operator new[](size_t size) {
    return ::operator new(size);
}
void operator delete(void* ptr) noexcept {
    if (ptr) {
        live_allocations.fetch_sub(1);
        std::free(ptr);
    }
}
void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}
void operator delete(void* ptr, size_t) noexcept {
    ::operator delete(ptr);
}
void operator delete[](void* ptr, size_t) noexcept {
    ::operator delete(ptr);
}
namespace kernel::scheduler {
void scheduler::publish_termination(thread* owner, int status) noexcept {
    owner->exit_code = status;
    owner->exited = true;
    owner->state.store(thread_state::TERMINATED, kernel::release);
}
thread* scheduler::current_thread() noexcept {
    return &current;
}
thread* scheduler::pin_thread(uint32_t) noexcept {
    return nullptr;
}

void scheduler::add_thread(thread* target) noexcept {
    ++queued;
    target->state.store(thread_state::READY, kernel::release);
}
void scheduler::unblock(thread* target) noexcept {
    irq_lock_guard guard(target->wait_lock);
    if (target->interruptible_wait && target->state.load(kernel::acquire) != thread_state::BLOCKED) return;
    add_thread(target);
}
void scheduler::schedule() noexcept {
    ++scheduled;
    if (current.state.load(kernel::acquire) != thread_state::BLOCKED || !on_schedule) std::abort();
    auto callback = on_schedule;
    on_schedule = nullptr;
    callback();
    if (current.state.load(kernel::acquire) != thread_state::READY) std::abort();
    current.state.store(thread_state::RUNNING, kernel::release);
}
void scheduler::finish_termination() noexcept {
    ++stopped;
}
int scheduler::signal_thread(int pid, int sig) noexcept {
    return process::signal_manager::post_signal(pid == 0 || pid == static_cast<int>(current.tid) ? &current : nullptr,
                                                sig);
}
} // namespace kernel::scheduler
namespace kernel::process {
bool raw_io_allowed() noexcept {
    return true;
}
bool ipc_send_allowed(uint32_t) noexcept {
    return true;
}
} // namespace kernel::process
namespace kernel::vfs {
void vfs_manager::close_all(scheduler::thread*) noexcept {
    ++closed;
    closed_with_resources = resources != 0 || live_allocations.load() != baseline;
}
} // namespace kernel::vfs
namespace kernel::memory {
uintptr_t vmm::get_user_phys(uintptr_t address, bool) noexcept {
    if (on_translate) {
        auto callback = on_translate;
        on_translate = nullptr;
        callback();
    }
    if (address < USER_BEGIN || address >= USER_BEGIN + sizeof(user_page)) return 0;
    return reinterpret_cast<uintptr_t>(user_page + (address - USER_BEGIN));
}
} // namespace kernel::memory
TEST(fatal_signal_waits_for_syscall_resources_to_unwind) {
    reset();
    ASSERT_EQ(read_call(interrupted_read), -4L);
    ASSERT(deferred);
    ASSERT_EQ(depth_seen, 1U);
    ASSERT_EQ(user_page[0], 0xA5);
    check_stopped();
}
TEST(nested_syscalls_defer_delivery_until_outermost_scope) {
    reset();
    const auto outer = [](long, long, long, long, long, long, long) -> long {
        resource held;
        ASSERT_EQ(read_call(interrupted_read), -4L);
        ASSERT_EQ(closed, 0U);
        ASSERT_EQ(current.kernel_depth.load(), 1U);
        return 0;
    };
    ASSERT_EQ(read_call(outer), -4L);
    ASSERT_EQ(depth_seen, 2U);
    check_stopped();
}
TEST(pending_fatal_signal_skips_marshalling_and_handler) {
    reset();
    ASSERT_EQ(signal_manager::post_signal(&current, 9), 0);
    ASSERT_EQ(read_call(success), -4L);
    ASSERT(!invoked);
    ASSERT_EQ(current.exit_code, 9);
    check_stopped();
}
TEST(fatal_post_during_copy_unwinds_buffers_before_delivery) {
    reset();
    on_translate = [] {
        ASSERT_EQ(signal_manager::post_signal(&current, 15), 0);
        ASSERT(!signal_manager::consume_fatal_signal(&current));
    };
    ASSERT_EQ(read_call(success), -4L);
    ASSERT(!invoked);
    check_stopped();
}
TEST(masked_term_defers_but_kill_cannot_be_masked) {
    reset();
    current.sig_mask.store(1U << 15);
    ASSERT_EQ(signal_manager::post_signal(&current, 15), 0);
    ASSERT_EQ(read_call(success), 1L);
    ASSERT_EQ(user_page[0], 0x42);
    ASSERT_EQ(closed, 0U);
    ASSERT_EQ(current.kernel_depth.load(), 0U);
    current.sig_mask.store(1U << 9);
    ASSERT_EQ(signal_manager::post_signal(&current, 9), 0);
    ASSERT_EQ(read_call(success), -4L);
    ASSERT_EQ(current.exit_code, 9);
    check_stopped();
}
TEST(validation_errors_restore_kernel_depth_without_delivery) {
    reset();
    ASSERT_EQ(checked_syscall(success, SYS_READ, 0, 1, 16, 0, 0, 0), -14L);
    ASSERT(!invoked);
    ASSERT_EQ(current.kernel_depth.load(), 0U);
    ASSERT_EQ(live_allocations.load(), baseline);
    ASSERT_EQ(closed, 0U);
}
TEST(futex_cancellation_unlinks_stack_waiter_before_scope_exit) {
    reset();
    on_schedule = [] {
        ASSERT(current.interruptible_wait);
        ASSERT_EQ(signal_manager::post_signal(&current, 15), 0);
        ASSERT(!signal_manager::consume_fatal_signal(&current));
    };
    ASSERT_EQ(checked_syscall(futex_handler, SYS_FUTEX, memory::USER_BEGIN, 0, 0, 0, 0, 0), -4L);
    ASSERT_EQ(queued, 1U);
    ASSERT(!current.interruptible_wait);
    ASSERT_EQ(sync::futex_wake(reinterpret_cast<uint32_t*>(memory::USER_BEGIN), 1), 0L);
    check_stopped();
}
TEST(futex_event_and_signal_race_does_not_enqueue_twice) {
    reset();
    on_schedule = [] {
        ASSERT_EQ(signal_manager::post_signal(&current, 9), 0);
        ASSERT_EQ(sync::futex_wake(reinterpret_cast<uint32_t*>(memory::USER_BEGIN), 1), 1L);
        ASSERT_EQ(queued, 1U);
    };
    ASSERT_EQ(checked_syscall(futex_handler, SYS_FUTEX, memory::USER_BEGIN, 0, 0, 0, 0, 0), -4L);
    ASSERT_EQ(sync::futex_wake(reinterpret_cast<uint32_t*>(memory::USER_BEGIN), 1), 0L);
    check_stopped();
}
TEST(futex_normal_wake_and_mismatch_leave_no_wait_record) {
    reset();
    on_schedule = [] { ASSERT_EQ(sync::futex_wake(reinterpret_cast<uint32_t*>(memory::USER_BEGIN), 1), 1L); };
    ASSERT_EQ(checked_syscall(futex_handler, SYS_FUTEX, memory::USER_BEGIN, 0, 0, 0, 0, 0), 0L);
    ASSERT(!current.interruptible_wait);
    ASSERT_EQ(sync::futex_wake(reinterpret_cast<uint32_t*>(memory::USER_BEGIN), 1), 0L);
    std::memset(user_page, 0, sizeof(user_page));
    ASSERT_EQ(checked_syscall(futex_handler, SYS_FUTEX, memory::USER_BEGIN, 0, 0, 0, 0, 0), -1L);
    ASSERT_EQ(scheduled, 1U);
    ASSERT_EQ(closed, 0U);
}
TEST(uninterruptible_kernel_wait_keeps_fatal_pending_until_completion) {
    reset();
    {
        kernel_scope scope(&current);
        current.state.store(thread_state::BLOCKED, kernel::release);
        ASSERT_EQ(signal_manager::post_signal(&current, 15), 0);
        ASSERT_EQ(queued, 0U);
        ASSERT(!signal_manager::consume_fatal_signal(&current));
        current.state.store(thread_state::RUNNING, kernel::release);
    }
    ASSERT(signal_manager::consume_fatal_signal(&current));
    ASSERT_EQ(closed, 1U);
    ASSERT(!signal_manager::consume_fatal_signal(&current));
    ASSERT_EQ(closed, 1U);
}
TEST(concurrent_signal_posts_keep_bits_and_enqueue_one_interruptible_wait) {
    reset();
    {
        kernel_scope scope(&current);
        ASSERT(current.begin_interruptible_wait());
        std::array<std::thread, 4> writers;
        for (unsigned i = 0; i < writers.size(); ++i)
            writers[i] = std::thread([i] {
                for (unsigned n = 0; n < 1000; ++n)
                    ASSERT_EQ(signal_manager::post_signal(&current, i & 1U ? 9 : 15), 0);
            });
        for (auto& writer : writers)
            writer.join();
        ASSERT_EQ(queued, 1U);
        ASSERT_EQ(current.sig_pending.load() & ((1U << 9) | (1U << 15)), (1U << 9) | (1U << 15));
        current.end_interruptible_wait();
        current.state.store(thread_state::RUNNING, kernel::release);
    }
    ASSERT(signal_manager::consume_fatal_signal(&current));
    ASSERT_EQ(current.exit_code, 9);
    ASSERT(!closed_with_resources);
}
int main() {
    return RUN_ALL_TESTS("Kernel cancellation");
}
