// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
namespace {
uint64_t requested_tick, last_tick;
unsigned sleeps;
kernel::scheduler::thread sleeper{};
bool cancel_sleep;
void reset() {
    kernel::time_manager::init();
    requested_tick = last_tick = sleeps = 0;
    sleeper.sig_pending.store(0);
    cancel_sleep = false;
}
} // namespace
namespace kernel {
void kwrite(const char*) noexcept {}
} // namespace kernel
namespace kernel::console {
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
} // namespace kernel::console
namespace kernel::scheduler {
thread* scheduler::current_thread() noexcept {
    return &sleeper;
}
void scheduler::check_sleepers(uint64_t tick) noexcept {
    last_tick = tick;
}
void scheduler::sleep_until(uint64_t tick) noexcept {
    requested_tick = tick;
    ++sleeps;
    if (cancel_sleep) {
        sleeper.sig_pending.store(1U << 15);
        return;
    }
    while (kernel::time_manager::get_ticks() < tick)
        kernel::time_manager::tick();
}
} // namespace kernel::scheduler
TEST(timer_wakes_sleep_queue) {
    reset();
    kernel::time_manager::tick();
    ASSERT_EQ(last_tick, 1ULL);
}
TEST(clock_tracks_one_second) {
    reset();
    for (int i = 0; i < 1001; ++i)
        kernel::time_manager::tick();
    kernel::timespec value{};
    kernel::time_manager::sys_clock_gettime(1, &value);
    ASSERT_EQ(value.tv_sec, 1);
    ASSERT_EQ(value.tv_nsec, 1000000);
}
TEST(sleep_blocks_once_and_rounds_up) {
    reset();
    kernel::timespec request{0, 1}, remaining{-1, -1};
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&request, &remaining), 0);
    ASSERT_EQ(sleeps, 1U);
    ASSERT_EQ(requested_tick, 1ULL);
    ASSERT_EQ(remaining.tv_nsec, 0);
}
TEST(zero_sleep_does_not_block) {
    reset();
    kernel::timespec request{};
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&request, nullptr), 0);
    ASSERT_EQ(sleeps, 0U);
}
TEST(invalid_sleep_rejected) {
    reset();
    kernel::timespec negative{-1, 0}, nanoseconds{0, 1000000000}, huge{INT64_MAX, 0};
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&negative, nullptr), -22);
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&nanoseconds, nullptr), -22);
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&huge, nullptr), -22);
    ASSERT_EQ(sleeps, 0U);
}
TEST(interrupted_sleep_returns_to_kernel_caller_for_cleanup) {
    reset();
    cancel_sleep = true;
    kernel::timespec request{60, 0}, remaining{-1, -1};
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&request, &remaining), -4);
    ASSERT_EQ(sleeps, 1U);
    ASSERT_EQ(remaining.tv_sec, -1);
    sleeper.sig_pending.store(0);
    cancel_sleep = false;
    request = {0, 1};
    ASSERT_EQ(kernel::time_manager::sys_nanosleep(&request, nullptr), 0);
}

int main() {
    return rucux_test::run_all_tests("Timer sleep");
}
