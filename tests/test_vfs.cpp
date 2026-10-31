// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <cstring>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/ramfs.hpp>
#include <kernel/vfs/vfs.hpp>

namespace {
using namespace kernel::vfs;
kernel::scheduler::thread current{};
kernel::scheduler::thread::file_descriptor descriptors[4]{};
vfs_ops ops{};
vfs_node node{};
bool has_current;
int callback_result;
unsigned truncated, synced;
uint64_t ticks;

void reset() {
    current.fd_table = descriptors;
    current.fd_count = 4;
    for (auto& fd : descriptors)
        fd = {};
    node = {};
    node.type = file_type::REGULAR;
    node.length = 128;
    node.ops = &ops;
    ops = {};
    ops.truncate = [](vfs_node* target, size_t length) {
        ++truncated;
        if (!callback_result) target->length = length;
        return callback_result;
    };
    ops.fsync = [](vfs_node*) {
        ++synced;
        return callback_result;
    };
    descriptors[1] = {&node, 99, 2};
    has_current = true;
    callback_result = 0;
    truncated = synced = 0;
}

vfs_node* ram_file(const char* name) {
    // Mounted RAMFS objects remain VFS-owned and reachable until test exit.
    auto* root = vfs_manager::get_root();
    if (!root) {
        root = ramfs::create_root();
        vfs_manager::set_root(root);
    }
    auto* file = ramfs::create_file(root, name, reinterpret_cast<const uint8_t*>("old-data"), 8);
    descriptors[1] = {file, 0, 2};
    return file;
}
} // namespace
namespace kernel {
void kwrite(const char*) noexcept {}
void kputc(char) noexcept {}
uint64_t time_manager::get_ticks() noexcept {
    return ticks;
}
namespace console {
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
} // namespace console
namespace scheduler {
thread* scheduler::current_thread() noexcept {
    return has_current ? &current : nullptr;
}
bool thread::ensure_fd_capacity(size_t) noexcept {
    return false;
}
void scheduler::sleep_until(uint64_t tick) noexcept {
    ticks = tick;
}
} // namespace scheduler
} // namespace kernel

TEST(vfs_dispatches_truncate_without_changing_the_offset) {
    reset();
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 37), 0);
    ASSERT_EQ(node.length, 37UL);
    ASSERT_EQ(descriptors[1].offset, 99UL);
    ASSERT_EQ(truncated, 1U);
    ASSERT_EQ(vfs_manager::sys_fsync(1), 0);
    ASSERT_EQ(synced, 1U);
}
TEST(vfs_propagates_callback_errors) {
    reset();
    for (int error : {-5, -16, -95}) {
        callback_result = error;
        ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), error);
        ASSERT_EQ(node.length, 128UL);
        ASSERT_EQ(vfs_manager::sys_fsync(1), error);
    }
}
TEST(vfs_rejects_invalid_descriptors_and_lengths) {
    reset();
    for (int fd : {-1, 0, 4, 999}) {
        ASSERT_EQ(vfs_manager::sys_ftruncate(fd, 0), -9);
        ASSERT_EQ(vfs_manager::sys_fsync(fd), -9);
    }
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, -1), -22);
    has_current = false;
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), -9);
    ASSERT_EQ(vfs_manager::sys_fsync(1), -9);
    ASSERT_EQ(truncated + synced, 0U);
}
TEST(vfs_truncate_requires_a_writable_regular_file) {
    reset();
    descriptors[1].flags = 0;
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), -22);
    ASSERT_EQ(vfs_manager::sys_fsync(1), 0); // Read-only descriptors may flush.
    descriptors[1].flags = 1;
    node.type = file_type::DIRECTORY;
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), -21);
    node.type = file_type::SOCKET;
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), -22);
    ASSERT_EQ(truncated, 0U);
}
TEST(vfs_missing_callbacks_do_not_report_success) {
    reset();
    ops.truncate = nullptr;
    ops.fsync = nullptr;
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), -95);
    ASSERT_EQ(vfs_manager::sys_fsync(1), -22);
    ASSERT_EQ(node.length, 128UL);
}
TEST(ramfs_truncate_changes_readable_eof_and_retains_the_fd_offset) {
    reset();
    auto* file = ram_file("truncate-eof");
    descriptors[1].offset = 3;
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 2), 0);
    ASSERT_EQ(file->length, 2UL);
    ASSERT_EQ(descriptors[1].offset, 3UL);
    char bytes[16]{};
    ASSERT_EQ(vfs_manager::sys_read(1, bytes, sizeof(bytes)), 0);
    descriptors[1].offset = 0;
    ASSERT_EQ(vfs_manager::sys_read(1, bytes, sizeof(bytes)), 2);
    ASSERT(std::memcmp(bytes, "ol", 2) == 0);
    ASSERT_EQ(vfs_manager::sys_fsync(1), 0);
}
TEST(ramfs_does_not_expose_discarded_data_when_rewriting_after_zero_truncate) {
    reset();
    ram_file("truncate-gap");
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 0), 0);
    descriptors[1].offset = 5;
    ASSERT_EQ(vfs_manager::sys_write(1, "x", 1), 1);
    descriptors[1].offset = 0;
    char bytes[8];
    ASSERT_EQ(vfs_manager::sys_read(1, bytes, sizeof(bytes)), 6);
    for (unsigned i = 0; i < 5; ++i)
        ASSERT_EQ(bytes[i], 0);
    ASSERT_EQ(bytes[5], 'x');
    ASSERT_EQ(vfs_manager::sys_ftruncate(1, 7), -95);
}
int main() {
    return RUN_ALL_TESTS("VFS persistence callbacks and RAMFS EOF");
}
