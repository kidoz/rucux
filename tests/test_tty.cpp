// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <cstring>
#undef putc
#undef putc_unlocked
#include <kernel/console.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/tty.hpp>
namespace {
kernel::scheduler::thread reader{};
bool woke, enrolled;
void reset() {
    kernel::vfs::tty::create();
    woke = enrolled = false;
    reader.state = kernel::scheduler::thread_state::RUNNING;
}
void input(const char* s) {
    while (*s)
        kernel::vfs::tty::feed_input(*s++);
}
kernel::vfs::vfs_node* node() {
    static auto* n = kernel::vfs::tty::create();
    return n;
}
} // namespace
namespace kernel::console {
void putc(char) noexcept {}
void putc_unlocked(char) noexcept {}
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
display_info get_display_info() noexcept {
    return {80, 25, 0, 0, false};
}
} // namespace kernel::console
namespace kernel::scheduler {
thread* scheduler::current_thread() noexcept {
    return &reader;
}
void scheduler::unblock(thread* t) noexcept {
    woke = true;
    t->state = thread_state::READY;
}
void scheduler::schedule() noexcept {
    enrolled = reader.state == thread_state::BLOCKED;
    input("wake\n");
    reader.state = thread_state::RUNNING;
}
} // namespace kernel::scheduler
TEST(canonical_carriage_return) {
    auto* n = node();
    reset();
    input("hello\r");
    char out[16]{};
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 6UL);
    ASSERT(strcmp(out, "hello\n") == 0);
}
TEST(erase_and_kill_line) {
    auto* n = node();
    reset();
    input("wrong\025abcx\177d\n");
    char out[16]{};
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 5UL);
    ASSERT(strcmp(out, "abcd\n") == 0);
}
TEST(erase_preserves_completed_line) {
    auto* n = node();
    reset();
    input("first\nsecond\025\bnext\n");
    char out[32]{};
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 6UL);
    ASSERT(strcmp(out, "first\n") == 0);
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 5UL);
}
TEST(eof_without_newline) {
    auto* n = node();
    reset();
    input("hi\004\004");
    char out[16]{};
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 2UL);
    ASSERT(strcmp(out, "hi") == 0);
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 0UL);
}
TEST(zero_length_read_never_blocks) {
    auto* n = node();
    reset();
    ASSERT_EQ(n->ops->read(n, 0, 0, nullptr), 0UL);
    ASSERT(!enrolled);
}
TEST(reader_enrolled_before_wakeup) {
    auto* n = node();
    reset();
    char out[16]{};
    ASSERT_EQ(n->ops->read(n, 0, sizeof(out), out), 5UL);
    ASSERT(enrolled);
    ASSERT(woke);
    ASSERT(strcmp(out, "wake\n") == 0);
}
TEST(full_line_keeps_delimiter_space) {
    auto* n = node();
    reset();
    for (int i = 0; i < 2000; ++i)
        kernel::vfs::tty::feed_input('x');
    input("\n");
    char out[1024]{};
    auto count = n->ops->read(n, 0, sizeof(out), out);
    ASSERT_EQ(count, 1023UL);
    ASSERT_EQ(out[count - 1], '\n');
}
int main() {
    return rucux_test::run_all_tests("TTY discipline");
}
