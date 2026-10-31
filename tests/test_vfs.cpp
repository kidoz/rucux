// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <climits>
#include <cstring>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/ramfs.hpp>
#include <kernel/vfs/vfs.hpp>
#include <uapi/kernel/stat.h>

namespace {
using namespace kernel::vfs;
kernel::scheduler::thread current{};
kernel::scheduler::thread::file_descriptor descriptors[4]{};
vfs_ops ops{};
vfs_node node{};
bool has_current;
int callback_result;
unsigned truncated, synced;
unsigned refreshed;
size_t refreshed_length;
int refresh_result;
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
    refreshed = 0;
    refresh_result = 0;
}

void set_refresh(size_t length, int result = 0) {
    refreshed_length = length;
    refresh_result = result;
    ops.refresh_metadata = [](vfs_node* target) {
        ++refreshed;
        if (refresh_result) return refresh_result;
        target->length = refreshed_length;
        return 0;
    };
}

struct MetadataMount {
    vfs_node* previous = vfs_manager::get_root();
    vfs_node root{};
    vfs_ops root_ops{};
    MetadataMount() {
        root.type = file_type::DIRECTORY;
        root.ops = &root_ops;
        root_ops.finddir = [](vfs_node*, const char* name) -> vfs_node* {
            return std::strcmp(name, "file") == 0 ? &node : nullptr;
        };
        vfs_manager::set_root(&root);
    }
    ~MetadataMount() { vfs_manager::set_root(previous); }
};

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

TEST(vfs_refreshes_metadata_before_stat_and_fstat) {
    reset();
    MetadataMount mount;
    set_refresh(37);
    struct stat info{};
    ASSERT_EQ(vfs_manager::sys_fstat(1, &info), 0);
    ASSERT_EQ(info.st_size, 37);
    ASSERT_EQ(info.st_blocks, 1);
    ASSERT_EQ(descriptors[1].offset, 99UL);
    set_refresh(0);
    ASSERT_EQ(vfs_manager::sys_stat("/file", &info), 0);
    ASSERT_EQ(info.st_size, 0);
    ASSERT_EQ(info.st_blocks, 0);
    ASSERT_EQ(refreshed, 2U);
}

TEST(vfs_refreshes_only_end_based_seeks) {
    reset();
    set_refresh(37, -5);
    ASSERT_EQ(vfs_manager::sys_lseek(1, 10, 0), 10L);
    ASSERT_EQ(vfs_manager::sys_lseek(1, -1, 1), 9L);
    ASSERT_EQ(refreshed, 0U);
    ASSERT_EQ(vfs_manager::sys_lseek(1, 0, 2), -5L);
    ASSERT_EQ(descriptors[1].offset, 9UL);
    set_refresh(37);
    ASSERT_EQ(vfs_manager::sys_lseek(1, -1, 2), 36L);
    ASSERT_EQ(descriptors[1].offset, 36UL);
    ASSERT_EQ(refreshed, 2U);
}

TEST(vfs_refresh_failures_preserve_stat_buffers_and_seek_offsets) {
    reset();
    MetadataMount mount;
    struct stat info;
    std::memset(&info, 0xA5, sizeof(info));
    const auto original = info;
    for (int error : {-5, -16}) {
        set_refresh(37, error);
        ASSERT_EQ(vfs_manager::sys_fstat(1, &info), error);
        ASSERT(std::memcmp(&info, &original, sizeof(info)) == 0);
        ASSERT_EQ(vfs_manager::sys_stat("/file", &info), error);
        ASSERT(std::memcmp(&info, &original, sizeof(info)) == 0);
        ASSERT_EQ(vfs_manager::sys_lseek(1, 0, 2), static_cast<long>(error));
        ASSERT_EQ(descriptors[1].offset, 99UL);
        ASSERT_EQ(node.length, 128UL);
    }
}

TEST(vfs_local_metadata_needs_no_refresh_callback) {
    reset();
    MetadataMount mount;
    struct stat info{};
    ASSERT_EQ(vfs_manager::sys_fstat(1, &info), 0);
    ASSERT_EQ(info.st_size, 128);
    ASSERT_EQ(vfs_manager::sys_stat("/file", &info), 0);
    ASSERT_EQ(info.st_size, 128);
    ASSERT_EQ(vfs_manager::sys_lseek(1, 0, 2), 128L);
    node.ops = nullptr;
    ASSERT_EQ(vfs_manager::sys_fstat(1, &info), 0);
    ASSERT_EQ(info.st_size, 128);
    ASSERT_EQ(vfs_manager::sys_lseek(1, -1, 2), 127L);
}

TEST(vfs_metadata_validates_arguments_before_refreshing) {
    reset();
    MetadataMount mount;
    set_refresh(37);
    struct stat info{};
    for (int fd : {-1, 0, 4, 999}) {
        ASSERT_EQ(vfs_manager::sys_fstat(fd, &info), -9);
        ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 2), -9L);
    }
    ASSERT_EQ(vfs_manager::sys_fstat(1, nullptr), -14);
    ASSERT_EQ(vfs_manager::sys_stat(nullptr, &info), -14);
    ASSERT_EQ(vfs_manager::sys_stat("/file", nullptr), -14);
    ASSERT_EQ(vfs_manager::sys_stat("/missing", &info), -2);
    ASSERT_EQ(vfs_manager::sys_lseek(1, 0, 3), -22L);
    ASSERT_EQ(refreshed, 0U);
    ASSERT_EQ(descriptors[1].offset, 99UL);
}

TEST(vfs_seeks_preserve_width_and_reject_overflow_without_moving_the_offset) {
    reset();
    ASSERT_EQ(vfs_manager::sys_lseek(1, LONG_MAX, 0), LONG_MAX);
    ASSERT_EQ(vfs_manager::sys_lseek(1, 1, 1), -75L);
    ASSERT_EQ(descriptors[1].offset, static_cast<size_t>(LONG_MAX));
    ASSERT_EQ(vfs_manager::sys_lseek(1, -LONG_MAX, 1), 0L);
    ASSERT_EQ(vfs_manager::sys_lseek(1, LONG_MIN, 1), -22L);
    ASSERT_EQ(descriptors[1].offset, 0UL);
    node.length = LONG_MAX;
    ASSERT_EQ(vfs_manager::sys_lseek(1, 1, 2), -75L);
    ASSERT_EQ(descriptors[1].offset, 0UL);
    node.length = SIZE_MAX;
    ASSERT_EQ(vfs_manager::sys_lseek(1, 0, 2), -75L);
    ASSERT_EQ(descriptors[1].offset, 0UL);
    ASSERT_EQ(vfs_manager::sys_lseek(1, LONG_MIN, 2), LONG_MAX);
    ASSERT_EQ(vfs_manager::sys_lseek(1, LONG_MAX, 2), -75L);
    ASSERT_EQ(descriptors[1].offset, static_cast<size_t>(LONG_MAX));
}
int main() {
    return RUN_ALL_TESTS("VFS persistence callbacks and RAMFS EOF");
}
