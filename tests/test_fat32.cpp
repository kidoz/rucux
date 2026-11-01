// SPDX-License-Identifier: MIT
// Production FAT32, an atomic-sector disk, and volatile allocations lost on reboot.
#include "test_harness.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/ata.hpp>
#include <kernel/vfs/fat32.hpp>
#include <uapi/kernel/stat.h>

namespace {
using namespace kernel::vfs;
constexpr uint32_t PARTITION = 7;
constexpr uint32_t ROOT = PARTITION + 4;
constexpr size_t SECTOR = 512;
constexpr size_t CAPACITY = 2048;
using Image = std::array<uint8_t, 80 * SECTOR>;
Image disk;
void* allocations[64]{};
size_t allocation_count;
size_t allocation_limit = SIZE_MAX;
size_t write_attempts;
size_t event_attempts;
size_t flush_attempts;
bool fail_flush;
size_t cut_at = SIZE_MAX;
bool cut_after;
bool offline;
bool invalid_io;
uint32_t fail_read = UINT32_MAX;
uint32_t fail_write = UINT32_MAX;
void (*on_read)();
vfs_node* reentrant_file;
long nested_result;
kernel::scheduler::thread current{};
kernel::scheduler::thread::file_descriptor descriptors[4]{};

void put16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8U);
}
void put32(uint8_t* p, uint32_t value) {
    put16(p, static_cast<uint16_t>(value));
    put16(p + 2, static_cast<uint16_t>(value >> 16U));
}
uint32_t get32(const uint8_t* p) {
    return p[0] | (static_cast<uint32_t>(p[1]) << 8U) | (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}
void reset_volatile() {
    vfs_manager::set_root(nullptr);
    current.fd_table = descriptors;
    current.fd_count = 4;
    for (auto& fd : descriptors)
        fd = {};
    for (size_t i = 0; i < allocation_count; ++i)
        std::free(allocations[i]);
    allocation_count = 0;
    allocation_limit = SIZE_MAX;
    write_attempts = 0;
    event_attempts = flush_attempts = 0;
    fail_flush = false;
    cut_at = SIZE_MAX;
    cut_after = offline = invalid_io = false;
    fail_read = fail_write = UINT32_MAX;
    on_read = nullptr;
}
struct Fixture {
    Fixture() {
        reset_volatile();
        disk.fill(0);
        auto* bpb = disk.data() + PARTITION * SECTOR;
        put16(bpb + 11, 512);
        bpb[13] = 2; // Two sectors/cluster exposes continuation after an inner-loop failure.
        put16(bpb + 14, 2);
        bpb[16] = 2; // FAT mirrors must stay unchanged.
        put32(bpb + 32, 64);
        put32(bpb + 36, 1);
        put32(bpb + 44, 2);
        bpb[510] = 0x55;
        bpb[511] = 0xAA;
        for (unsigned copy = 0; copy < 2; ++copy) {
            auto* fat = disk.data() + (PARTITION + 2 + copy) * SECTOR;
            put32(fat, 0x0FFFFFF8);
            put32(fat + 4, 0x0FFFFFFF);
            put32(fat + 8, 0x0FFFFFFF);  // root
            put32(fat + 12, 5);          // fragmented journal: 3 -> 5
            put32(fat + 16, 0x0FFFFFFF); // other file
            put32(fat + 20, 0x0FFFFFFF);
        }
        auto* entry = disk.data() + ROOT * SECTOR;
        std::memcpy(entry, "JOURNAL LOG", 11);
        entry[11] = 0x20;
        put16(entry + 26, 3);
        put32(entry + 28, 128);
        std::memcpy(entry + 32, "OTHER   BIN", 11);
        entry[43] = 0x20;
        put16(entry + 58, 4);
        put32(entry + 60, 512);
        for (size_t i = 0; i < CAPACITY; ++i)
            disk[physical(i)] = static_cast<uint8_t>(i * 13 + 17);
        std::memset(disk.data() + (ROOT + 4) * SECTOR, 0x5A, 1024);
    }
    ~Fixture() { reset_volatile(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    static size_t physical(size_t offset) {
        const auto lba = offset < 1024 ? ROOT + 2 : ROOT + 6;
        return lba * SECTOR + offset % 1024;
    }
    static vfs_node* open() {
        auto* root = fat32::mount(PARTITION);
        ASSERT(root);
        vfs_node* file = nullptr;
        ASSERT_EQ(vfs_manager::lookup(root, "journal.log", file), 0);
        ASSERT(file);
        root->release();
        return file;
    }
};
} // namespace

extern "C" void* kmalloc(size_t size) noexcept {
    if (allocation_count >= allocation_limit) return nullptr;
    if (allocation_count == 64) std::abort();
    auto* result = std::malloc(size);
    if (result) allocations[allocation_count++] = result;
    return result;
}
extern "C" void kfree(void* pointer) noexcept {
    if (!pointer) return;
    for (size_t i = 0; i < allocation_count; ++i) {
        if (allocations[i] != pointer) continue;
        allocations[i] = allocations[--allocation_count];
        std::free(pointer);
        return;
    }
    std::abort();
}
namespace kernel {
void kwrite(const char*) noexcept {}
void kputc(char) noexcept {}
uint64_t time_manager::get_ticks() noexcept {
    return 0;
}
namespace scheduler {
thread* scheduler::current_thread() noexcept {
    return &current;
}
thread* scheduler::pin_thread(uint32_t) noexcept {
    return nullptr;
}

void scheduler::unblock(thread* target) noexcept {
    target->state.store(thread_state::READY);
}
void scheduler::schedule() noexcept {
    std::abort();
}
void scheduler::add_thread(thread* target) noexcept {
    target->state.store(thread_state::READY, kernel::release);
}
int scheduler::signal_thread(int, int) noexcept {
    return -3;
}
bool thread::ensure_fd_capacity(size_t count) noexcept {
    return fd_table && fd_count >= count;
}
void scheduler::sleep_until(uint64_t) noexcept {}
} // namespace scheduler
namespace console {
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
} // namespace console
namespace vfs::ata {
bool read_sectors(uint32_t lba, uint8_t count, void* buffer) noexcept {
    if (offline) return false;
    if (on_read) {
        auto callback = on_read;
        on_read = nullptr;
        callback();
    }
    if (!buffer || !count || lba >= 80 || count > 80 - lba) {
        invalid_io = true;
        return false;
    }
    if (lba == fail_read) {
        fail_read = UINT32_MAX;
        return false;
    }
    std::memcpy(buffer, disk.data() + lba * SECTOR, count * SECTOR);
    return true;
}
bool write_sectors(uint32_t lba, uint8_t count, const void* buffer) noexcept {
    if (offline) return false;
    if (!buffer || count != 1 || lba >= 80) {
        invalid_io = true;
        return false;
    }
    if (lba == fail_write) {
        fail_write = UINT32_MAX;
        return false;
    }
    ++write_attempts;
    const bool interrupted = event_attempts++ == cut_at;
    if (!interrupted || cut_after) std::memcpy(disk.data() + lba * SECTOR, buffer, SECTOR);
    if (interrupted) offline = true;
    return !offline;
}
bool flush() noexcept {
    if (offline) return false;
    ++flush_attempts;
    if (event_attempts++ == cut_at) offline = true;
    return !offline && !fail_flush;
}
} // namespace vfs::ata
} // namespace kernel

TEST(fat32_stops_at_first_data_read_failure) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY);
    auto* file = Fixture::open();
    std::array<uint8_t, 1200> data{};
    fail_read = ROOT + 3;
    ASSERT_EQ(file->ops->write(file, 0, data.size(), data.data()), 512L);
    ASSERT_EQ(write_attempts, 1UL);
}

TEST(fat32_fatal_signal_defers_until_io_guard_and_temporary_references_unwind) {
    Fixture fixture;
    current.user_entry = reinterpret_cast<void*>(1);
    current.state.store(kernel::scheduler::thread_state::RUNNING, kernel::release);
    vfs_manager::set_root(fat32::mount(PARTITION));
    const auto baseline = allocation_count;
    const int fd = vfs_manager::sys_open("/journal.log", 0);
    ASSERT_EQ(fd, 0);
    {
        kernel::scheduler::kernel_scope scope(&current);
        on_read = [] {
            ASSERT_EQ(kernel::process::signal_manager::post_signal(&current, 15), 0);
            ASSERT(!kernel::process::signal_manager::consume_fatal_signal(&current));
            ASSERT(vfs_manager::get_fd_node(0));
        };
        uint8_t byte{};
        ASSERT_EQ(vfs_manager::sys_read(fd, &byte, 1), 1L);
        ASSERT_EQ(byte, 17);
        ASSERT_EQ(allocation_count, baseline + 4);
    }
    ASSERT(kernel::process::signal_manager::consume_fatal_signal(&current));
    ASSERT(vfs_manager::get_fd_node(fd) == nullptr);
    ASSERT_EQ(allocation_count, baseline);
    // A new operation succeeds after cancellation; the device reservation was
    // released by normal stack unwinding before descriptor cleanup.
    current.state.store(kernel::scheduler::thread_state::RUNNING, kernel::release);
    struct stat info{};
    ASSERT_EQ(vfs_manager::sys_stat("/journal.log", &info), 0);
    ASSERT_EQ(info.st_size, 128);
    ASSERT_EQ(allocation_count, baseline);
}

TEST(fat32_repeated_stat_and_open_close_return_allocations_to_mount_baseline) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const auto baseline = allocation_count;
    for (unsigned i = 0; i < 32; ++i) {
        struct stat info{};
        ASSERT_EQ(vfs_manager::sys_stat("/journal.log", &info), 0);
        ASSERT_EQ(info.st_size, 128);
        ASSERT_EQ(allocation_count, baseline);
        const int fd = vfs_manager::sys_open("/journal.log", 0);
        ASSERT_EQ(fd, 0);
        ASSERT_EQ(vfs_manager::sys_close(fd), 0);
        ASSERT_EQ(allocation_count, baseline);
    }
}

TEST(fat32_nested_lookup_releases_intermediates_and_failure_results) {
    Fixture fixture;
    auto* entry = disk.data() + ROOT * SECTOR + 64;
    std::memcpy(entry, "SUBDIR     ", 11);
    entry[11] = 0x10;
    put16(entry + 26, 6);
    put32(disk.data() + (PARTITION + 2) * SECTOR + 24, 0x0FFFFFFF);
    std::memcpy(disk.data() + (ROOT + 8) * SECTOR, disk.data() + ROOT * SECTOR, 32);
    vfs_manager::set_root(fat32::mount(PARTITION));
    const auto baseline = allocation_count;
    for (unsigned i = 0; i < 32; ++i) {
        struct stat info{};
        ASSERT_EQ(vfs_manager::sys_stat("/subdir/journal.log", &info), 0);
        ASSERT_EQ(allocation_count, baseline);
        ASSERT_EQ(vfs_manager::sys_open("/subdir/journal.log/", 0), -20);
        ASSERT_EQ(allocation_count, baseline);
        ASSERT_EQ(vfs_manager::sys_open("/subdir/missing.bin", 0), -2);
        ASSERT_EQ(allocation_count, baseline);
        fail_read = ROOT + 8;
        ASSERT_EQ(vfs_manager::sys_open("/subdir/journal.log", 0), -5);
        ASSERT_EQ(allocation_count, baseline);
        fail_read = ROOT + 8;
        info.st_size = -123;
        ASSERT_EQ(vfs_manager::sys_stat("/subdir/journal.log", &info), -5);
        ASSERT_EQ(info.st_size, -123);
        ASSERT_EQ(allocation_count, baseline);
    }
    for (size_t extra = 0; extra < 8; ++extra) {
        allocation_limit = baseline + extra;
        ASSERT_EQ(vfs_manager::sys_open("/subdir/journal.log", 0), -12);
        ASSERT_EQ(allocation_count, baseline);
    }
    allocation_limit = SIZE_MAX;
    // A refresh error occurs after both successful lookups acquired references.
    on_read = [] { on_read = [] { on_read = [] { on_read = [] { fail_read = ROOT + 8; }; }; }; };
    struct stat info{};
    ASSERT_EQ(vfs_manager::sys_stat("/subdir/journal.log", &info), -5);
    ASSERT_EQ(allocation_count, baseline);
}

TEST(fat32_failed_open_releases_nodes_when_fd_growth_fails) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const auto baseline = allocation_count;
    vfs_node occupied{}; // Permanent fake entries occupy the whole hosted table.
    for (auto& descriptor : descriptors)
        descriptor.node = &occupied;
    for (unsigned i = 0; i < 32; ++i) {
        ASSERT_EQ(vfs_manager::sys_open("/journal.log", 0), -12);
        ASSERT_EQ(allocation_count, baseline);
        ASSERT_EQ(vfs_manager::sys_open("/journal.log", 00200000), -20);
        ASSERT_EQ(allocation_count, baseline);
    }
    vfs_manager::close_all(&current);
}

TEST(fat32_file_reference_keeps_mount_geometry_alive_after_root_release) {
    Fixture fixture;
    auto* root = fat32::mount(PARTITION);
    ASSERT(root);
    vfs_node* file = nullptr;
    ASSERT_EQ(vfs_manager::lookup(root, "journal.log", file), 0);
    root->name = "mount-point"; // The destructor owns the original allocation.
    root->release();
    ASSERT_EQ(allocation_count, 5UL); // File's four allocations and shared geometry.
    uint8_t byte{};
    ASSERT_EQ(file->ops->read(file, 0, 1, &byte), 1L);
    ASSERT_EQ(byte, 17);
    file->retain();
    file->release();
    ASSERT_EQ(allocation_count, 5UL);
    file->release();
    ASSERT_EQ(allocation_count, 0UL);
}

TEST(fat32_copied_descriptors_survive_parent_close_and_release_on_exit) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const auto baseline = allocation_count;
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT_EQ(fd, 0);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 7, 0), 7L);
    kernel::scheduler::thread child{};
    kernel::scheduler::thread::file_descriptor child_fds[4]{};
    child.fd_table = child_fds;
    child.fd_count = 4;
    ASSERT_EQ(vfs_manager::copy_fds(&current, &child), 0);
    auto* file = static_cast<vfs_node*>(child_fds[fd].node);
    ASSERT(file == vfs_manager::get_fd_node(fd));
    ASSERT_EQ(child_fds[fd].offset, 7UL);
    ASSERT_EQ(child_fds[fd].flags, 2);
    ASSERT_EQ(vfs_manager::sys_close(fd), 0);
    uint8_t byte{};
    ASSERT_EQ(file->ops->read(file, 0, 1, &byte), 1L);
    ASSERT_EQ(allocation_count, baseline + 4);
    vfs_manager::close_all(&child);
    ASSERT_EQ(allocation_count, baseline);
    ASSERT(child_fds[fd].node == nullptr);
    vfs_manager::close_all(&child);
    ASSERT_EQ(allocation_count, baseline);
    vfs_manager::set_root(nullptr);
    ASSERT_EQ(allocation_count, 0UL);
}

TEST(fat32_root_retains_namespace_reference_across_lookups) {
    Fixture fixture;
    // The namespace owns the original mount reference; each lookup owns another.
    auto* root = fat32::mount(PARTITION);
    ASSERT(root);
    vfs_manager::set_root(root);
    const auto baseline = allocation_count;
    for (unsigned i = 0; i < 32; ++i) {
        vfs_node* result = nullptr;
        ASSERT_EQ(vfs_manager::resolve_path("/", result), 0);
        ASSERT(result == root);
        result->release();
        ASSERT_EQ(allocation_count, baseline);
    }
    vfs_manager::set_root(nullptr);
    ASSERT_EQ(allocation_count, 0UL);
}

TEST(fat32_checked_lookup_distinguishes_missing_names_and_invalid_short_names) {
    Fixture fixture;
    auto* root = fat32::mount(PARTITION);
    ASSERT(root);
    vfs_node* result = root;
    ASSERT_EQ(vfs_manager::lookup(root, "missing.bin", result), -2);
    ASSERT(result == nullptr);
    ASSERT_EQ(vfs_manager::lookup(root, "long-name.bin", result), -36);
    ASSERT_EQ(vfs_manager::lookup(root, "file.long", result), -36);
    ASSERT_EQ(vfs_manager::lookup(root, "a.b.c", result), -22);
    ASSERT_EQ(vfs_manager::lookup(root, "JOURNAL.LOG", result), 0);
    ASSERT(result);
    ASSERT_EQ(result->length, 128UL);
    ASSERT(!invalid_io);
}

TEST(fat32_lookup_contention_reaches_open_and_stat_without_changing_outputs) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    on_read = [] {
        ASSERT_EQ(vfs_manager::sys_open("/journal.log", 0), -16);
        ASSERT(vfs_manager::get_fd_node(0) == nullptr);
        struct stat info{};
        info.st_size = -123;
        ASSERT_EQ(vfs_manager::sys_stat("/journal.log", &info), -16);
        ASSERT_EQ(info.st_size, -123);
    };
    const int fd = vfs_manager::sys_open("/journal.log", 0);
    ASSERT_EQ(fd, 0);
    ASSERT_EQ(vfs_manager::sys_close(fd), 0);
}

TEST(fat32_directory_chain_errors_are_not_missing_entries) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        Fixture fixture;
        auto* root = fat32::mount(PARTITION);
        ASSERT(root);
        // No end marker: both directory sectors must be scanned before FAT advance.
        for (size_t offset = 0; offset < 1024; offset += 32)
            disk[ROOT * SECTOR + offset] = 0xE5;
        if (mode == 0) fail_read = ROOT + 1;
        if (mode == 1) fail_read = PARTITION + 2;
        if (mode == 2) put32(disk.data() + (PARTITION + 2) * SECTOR + 8, 2); // Cycle.
        if (mode == 3) put32(disk.data() + (PARTITION + 2) * SECTOR + 8, 0x0FFFFFF7);
        vfs_node* result = root;
        ASSERT_EQ(vfs_manager::lookup(root, "missing.bin", result), -5);
        ASSERT(result == nullptr);
        ASSERT(!invalid_io);
    }
    Fixture fixture;
    auto* root = fat32::mount(PARTITION);
    ASSERT(root);
    for (size_t offset = 0; offset < 1024; offset += 32)
        disk[ROOT * SECTOR + offset] = 0xE5;
    vfs_node* result = root;
    ASSERT_EQ(vfs_manager::lookup(root, "missing.bin", result), -2); // Genuine EOC.
    ASSERT(result == nullptr);
}

TEST(fat32_lookup_allocation_failure_reaches_open_and_stat) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    allocation_limit = allocation_count;
    ASSERT_EQ(vfs_manager::sys_open("/journal.log", 0), -12);
    ASSERT(vfs_manager::get_fd_node(0) == nullptr);
    struct stat info{};
    info.st_size = -123;
    ASSERT_EQ(vfs_manager::sys_stat("/journal.log", &info), -12);
    ASSERT_EQ(info.st_size, -123);
}

TEST(fat32_directory_read_failure_reaches_open_and_stat_instead_of_missing_path) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    fail_read = ROOT;
    ASSERT_EQ(vfs_manager::sys_open("/journal.log", 0), -5);
    ASSERT(vfs_manager::get_fd_node(0) == nullptr);
    struct stat info{};
    info.st_size = -123;
    fail_read = ROOT;
    ASSERT_EQ(vfs_manager::sys_stat("/journal.log", &info), -5);
    ASSERT_EQ(info.st_size, -123);
}

TEST(fat32_vfs_first_read_failure_is_not_eof_and_does_not_move_the_offset) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 0);
    ASSERT(fd >= 0);
    std::array<uint8_t, 16> bytes;
    bytes.fill(0xA5);
    fail_read = ROOT + 2;
    ASSERT_EQ(vfs_manager::sys_read(fd, bytes.data(), bytes.size()), -5L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 0L);
    for (const auto byte : bytes)
        ASSERT_EQ(byte, 0xA5);
    ASSERT_EQ(vfs_manager::sys_read(fd, bytes.data(), bytes.size()), 16L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 16L);
}

TEST(fat32_vfs_partial_read_acknowledges_only_copied_bytes) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY);
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 0);
    ASSERT(fd >= 0);
    std::array<uint8_t, 1200> bytes;
    bytes.fill(0xA5);
    fail_read = ROOT + 3;
    ASSERT_EQ(vfs_manager::sys_read(fd, bytes.data(), bytes.size()), 512L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 512L);
    for (size_t i = 0; i < 512; ++i)
        ASSERT_EQ(bytes[i], disk[Fixture::physical(i)]);
    for (size_t i = 512; i < bytes.size(); ++i)
        ASSERT_EQ(bytes[i], 0xA5);
    ASSERT_EQ(vfs_manager::sys_read(fd, bytes.data(), 16), 16L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 528L);
}

TEST(fat32_vfs_ordinary_write_errors_and_acknowledged_prefixes_preserve_offsets) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT(fd >= 0);
    fail_write = ROOT + 2;
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -5L);
    fail_read = ROOT;
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -5L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 0L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 128, 0), 128L);
    fail_write = ROOT;
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -5L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 128L);
    std::array<uint8_t, 64> bytes{};
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 96, 0), 96L);
    fail_write = ROOT;
    ASSERT_EQ(vfs_manager::sys_write(fd, bytes.data(), bytes.size()), 32L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 128L);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

TEST(fat32_vfs_busy_reads_and_writes_are_errors_without_offset_changes) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT_EQ(fd, 0);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 17, 0), 17L);
    static long read_result, write_result;
    static uint8_t byte;
    byte = 0xA5;
    on_read = [] {
        read_result = vfs_manager::sys_read(0, &byte, 1);
        write_result = vfs_manager::sys_write(0, "x", 1);
    };
    ASSERT_EQ(vfs_manager::sys_fsync(fd), 0);
    ASSERT_EQ(read_result, -16L);
    ASSERT_EQ(write_result, -16L);
    ASSERT_EQ(byte, 0xA5);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 17L);
    ASSERT_EQ(write_attempts, 0UL);
}

TEST(fat32_vfs_eof_and_full_capacity_have_distinct_results) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY);
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT(fd >= 0);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 2), static_cast<long>(CAPACITY));
    uint8_t byte;
    ASSERT_EQ(vfs_manager::sys_read(fd, &byte, 1), 0L);
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -28L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), static_cast<long>(CAPACITY));
}

TEST(fat32_stops_at_first_data_write_failure) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY);
    auto* file = Fixture::open();
    std::array<uint8_t, 1200> data{};
    fail_write = ROOT + 3;
    ASSERT_EQ(file->ops->write(file, 0, data.size(), data.data()), 512L);
    ASSERT_EQ(write_attempts, 1UL);
}

TEST(fat32_does_not_acknowledge_growth_after_metadata_read_failure) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, 700> data{};
    fail_read = ROOT;
    ASSERT_EQ(file->ops->write(file, 128, data.size(), data.data()), -5L);
    ASSERT_EQ(file->length, 128UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

TEST(fat32_does_not_acknowledge_growth_after_metadata_write_failure) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, 700> data{};
    fail_write = ROOT;
    ASSERT_EQ(file->ops->write(file, 96, data.size(), data.data()), 32L);
    ASSERT_EQ(file->length, 128UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

TEST(fat32_read_clamps_overflowing_lengths) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, CAPACITY> buffer{};
    ASSERT_EQ(file->ops->read(file, 64, SIZE_MAX, buffer.data()), 64L);
    for (size_t i = 0; i < 64; ++i)
        ASSERT_EQ(buffer[i], disk[Fixture::physical(i + 64)]);
    ASSERT_EQ(file->ops->read(file, SIZE_MAX, 1, buffer.data()), 0L);
}

TEST(fat32_rejects_holes_and_short_writes_at_allocated_capacity) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, CAPACITY * 2> buffer{};
    ASSERT_EQ(file->ops->write(file, 129, 1, buffer.data()), -95L);
    ASSERT_EQ(write_attempts, 0UL);
    ASSERT_EQ(file->ops->write(file, SIZE_MAX, 1, buffer.data()), -75L);
    ASSERT_EQ(file->ops->write(file, 0, 0, nullptr), 0L);
    ASSERT_EQ(file->ops->write(file, 128, buffer.size(), buffer.data()), static_cast<long>(CAPACITY - 128));
    ASSERT_EQ(file->length, CAPACITY);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), CAPACITY);
    ASSERT_EQ(file->ops->write(file, CAPACITY, 1, buffer.data()), -28L);
    ASSERT(!invalid_io);
}

TEST(fat32_rejects_invalid_volume_geometry_before_allocating) {
    for (unsigned mode = 0; mode < 17; ++mode) {
        Fixture fixture;
        auto* bpb = disk.data() + PARTITION * SECTOR;
        switch (mode) {
        case 0:
            bpb[510] = 0;
            break;
        case 1:
            put16(bpb + 11, 1024);
            break;
        case 2:
            bpb[13] = 0;
            break;
        case 3:
            bpb[13] = 3;
            break;
        case 4:
            bpb[13] = 129;
            break;
        case 5:
            put16(bpb + 14, 0);
            break;
        case 6:
            bpb[16] = 0;
            break;
        case 7:
            bpb[16] = 3;
            break;
        case 8:
            put32(bpb + 36, 0);
            break;
        case 9:
            put32(bpb + 32, 3);
            break;
        case 10:
            put32(bpb + 32, UINT32_MAX);
            break;
        case 11:
            put32(bpb + 32, 0x0FFFFFFF);
            break;
        case 12:
            put32(bpb + 44, 0x0FFFFFFF);
            break;
        case 13:
            put16(bpb + 17, 32);
            break;
        case 14:
            put16(bpb + 22, 1);
            break;
        case 15:
            put16(bpb + 42, 1);
            break;
        case 16:
            put16(bpb + 40, 0x81);
            break;
        }
        ASSERT(!fat32::mount(PARTITION));
        ASSERT_EQ(allocation_count, 0UL);
        ASSERT(!invalid_io);
    }
}

TEST(fat32_rejects_cyclic_out_of_range_and_unreadable_chains) {
    for (unsigned mode = 0; mode < 5; ++mode) {
        Fixture fixture;
        auto* root = fat32::mount(PARTITION);
        ASSERT(root);
        if (mode == 0) put32(disk.data() + (PARTITION + 2) * SECTOR + 12, 3);
        if (mode == 1) put32(disk.data() + (PARTITION + 2) * SECTOR + 12, 90);
        if (mode == 2) put32(disk.data() + (PARTITION + 2) * SECTOR + 12, 0x0FFFFFF7);
        if (mode == 3) put32(disk.data() + ROOT * SECTOR + 28, CAPACITY + 1);
        if (mode == 4) fail_read = PARTITION + 2;
        vfs_node* file = root;
        ASSERT_EQ(vfs_manager::lookup(root, "journal.log", file), -5);
        ASSERT(file == nullptr);
        ASSERT(!invalid_io);
    }
    Fixture fixture;
    put32(disk.data() + (PARTITION + 2) * SECTOR + 8, 2);
    ASSERT(!fat32::mount(PARTITION));
    ASSERT_EQ(allocation_count, 0UL);
}

TEST(fat32_refreshes_stale_handles_and_persists_reset_before_rewriting) {
    Fixture fixture;
    auto* first = Fixture::open();
    auto* second = Fixture::open();
    std::array<uint8_t, 700> buffer{};
    ASSERT_EQ(first->ops->write(first, 128, buffer.size(), buffer.data()), static_cast<long>(buffer.size()));
    ASSERT_EQ(second->ops->write(second, 0, 16, buffer.data()), 16L);
    ASSERT_EQ(second->length, 828UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 828U);
    ASSERT_EQ(second->ops->truncate(second, 0), 0);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 0U);
    ASSERT_EQ(second->ops->write(second, 0, 5, buffer.data()), 5L);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 5U);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 60), 512U);
}

TEST(fat32_reentrant_io_fails_without_interleaving_disk_commands) {
    Fixture fixture;
    reentrant_file = Fixture::open();
    nested_result = -1;
    on_read = [] { nested_result = reentrant_file->ops->write(reentrant_file, 0, 1, "x"); };
    ASSERT_EQ(reentrant_file->ops->write(reentrant_file, 0, 1, "y"), 1L);
    ASSERT_EQ(nested_result, -16L);
    ASSERT_EQ(write_attempts, 1UL);
    ASSERT_EQ(disk[Fixture::physical(0)], 'y');
}

TEST(fat32_allocation_failures_release_partial_nodes) {
    for (size_t limit = 0; limit < 5; ++limit) {
        Fixture fixture;
        allocation_limit = limit;
        ASSERT(!fat32::mount(PARTITION));
        ASSERT_EQ(allocation_count, 0UL);
    }
    Fixture fixture;
    auto* root = fat32::mount(PARTITION);
    ASSERT(root);
    const auto baseline = allocation_count;
    for (size_t extra = 0; extra < 4; ++extra) {
        allocation_limit = baseline + extra;
        vfs_node* file = root;
        ASSERT_EQ(vfs_manager::lookup(root, "journal.log", file), -12);
        ASSERT(file == nullptr);
        ASSERT_EQ(allocation_count, baseline);
    }
}

TEST(fat32_shrink_is_durable_without_a_later_write_and_retains_allocated_capacity) {
    Fixture fixture;
    auto* file = Fixture::open();
    auto expected = disk;
    put32(expected.data() + ROOT * SECTOR + 28, 37);
    ASSERT_EQ(file->ops->truncate(file, 37), 0);
    ASSERT_EQ(file->length, 37UL);
    ASSERT(disk == expected); // No data sector, FAT entry, or neighboring entry changed.
    ASSERT_EQ(file->ops->fsync(file), 0);
    ASSERT_EQ(flush_attempts, 1UL);
    reset_volatile();
    file = Fixture::open();
    ASSERT_EQ(file->length, 37UL);
    ASSERT_EQ(file->ops->truncate(file, 0), 0);
    reset_volatile();
    file = Fixture::open();
    ASSERT_EQ(file->length, 0UL);
    std::array<uint8_t, CAPACITY> bytes{};
    ASSERT_EQ(file->ops->write(file, 0, bytes.size(), bytes.data()), static_cast<long>(bytes.size()));
    ASSERT_EQ(file->length, CAPACITY);
}

TEST(fat32_failed_metadata_updates_do_not_acknowledge_shrinking) {
    Fixture fixture;
    auto* file = Fixture::open();
    fail_read = ROOT;
    ASSERT_EQ(file->ops->truncate(file, 37), -5);
    ASSERT_EQ(file->length, 128UL);
    fail_write = ROOT;
    ASSERT_EQ(file->ops->truncate(file, 37), -5);
    ASSERT_EQ(file->length, 128UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
    ASSERT_EQ(file->ops->truncate(file, 129), -95);
    ASSERT_EQ(file->ops->truncate(file, SIZE_MAX), -95);
    ASSERT_EQ(file->length, 128UL);
}

TEST(fat32_flush_failures_reach_the_caller) {
    Fixture fixture;
    auto* file = Fixture::open();
    fail_flush = true;
    ASSERT_EQ(file->ops->fsync(file), -5);
    ASSERT_EQ(file->ops->truncate(file, 128), -5); // Even a no-op checks flush completion.
    ASSERT_EQ(file->length, 128UL);
    fail_flush = false;
    ASSERT_EQ(file->ops->fsync(file), 0);
    fail_read = ROOT;
    ASSERT_EQ(file->ops->fsync(file), -5);
}

TEST(fat32_stale_readers_and_writers_observe_a_durable_shrink) {
    Fixture fixture;
    auto* first = Fixture::open();
    auto* stale = Fixture::open();
    ASSERT_EQ(first->ops->truncate(first, 37), 0);
    char bytes[256]{};
    ASSERT_EQ(stale->ops->read(stale, 0, sizeof(bytes), bytes), 37L);
    ASSERT_EQ(stale->ops->truncate(stale, 100), -95);
    ASSERT_EQ(stale->ops->write(stale, 38, 1, "x"), -95L); // A stale EOF cannot permit a gap.
    ASSERT_EQ(stale->ops->write(stale, 37, 1, "x"), 1L);
    ASSERT_EQ(first->ops->fsync(first), 0);
    ASSERT_EQ(first->length, 38UL);
}

TEST(fat32_vfs_existing_handles_report_shrink_and_append_at_the_new_eof) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int writer = vfs_manager::sys_open("/journal.log", 2);
    const int observer = vfs_manager::sys_open("/journal.log", 2);
    const int seeker = vfs_manager::sys_open("/journal.log", 2);
    ASSERT(writer >= 0 && observer >= 0 && seeker >= 0);
    ASSERT(vfs_manager::get_fd_node(writer) != vfs_manager::get_fd_node(observer));
    ASSERT_EQ(vfs_manager::sys_ftruncate(writer, 37), 0);
    struct stat info{};
    ASSERT_EQ(vfs_manager::sys_fstat(observer, &info), 0);
    ASSERT_EQ(info.st_size, 37);
    ASSERT_EQ(vfs_manager::sys_lseek(seeker, 0, 2), 37);
    ASSERT_EQ(vfs_manager::sys_write(seeker, "x", 1), 1);
    ASSERT_EQ(vfs_manager::sys_fstat(writer, &info), 0);
    ASSERT_EQ(info.st_size, 38);
    ASSERT_EQ(vfs_manager::sys_stat("/journal.log", &info), 0);
    ASSERT_EQ(info.st_size, 38);
    ASSERT_EQ(vfs_manager::sys_ftruncate(writer, 0), 0);
    ASSERT_EQ(vfs_manager::sys_lseek(observer, 0, 2), 0);
    ASSERT_EQ(vfs_manager::sys_write(observer, "abc", 3), 3);
    ASSERT_EQ(vfs_manager::sys_fstat(seeker, &info), 0);
    ASSERT_EQ(info.st_size, 3);
    ASSERT_EQ(disk[Fixture::physical(0)], 'a');
    ASSERT_EQ(disk[Fixture::physical(2)], 'c');
}

TEST(fat32_append_handles_ignore_old_offsets_and_preserve_existing_contents) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int first = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    const int second = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    ASSERT(first >= 0 && second >= 0);
    const auto before = disk;
    ASSERT_EQ(vfs_manager::sys_write(first, "AB", 2), 2L);
    ASSERT_EQ(vfs_manager::sys_write(second, "cd", 2), 2L);
    ASSERT_EQ(vfs_manager::sys_lseek(first, 0, 0), 0L);
    ASSERT_EQ(vfs_manager::sys_write(first, "!", 1), 1L);
    ASSERT_EQ(vfs_manager::sys_lseek(first, 0, 1), 133L);
    ASSERT_EQ(vfs_manager::sys_lseek(second, 0, 1), 132L);
    struct stat info{};
    ASSERT_EQ(vfs_manager::sys_fstat(second, &info), 0);
    ASSERT_EQ(info.st_size, 133);
    for (size_t i = 0; i < 128; ++i)
        ASSERT_EQ(disk[Fixture::physical(i)], before[Fixture::physical(i)]);
    const char expected[] = "ABcd!";
    for (size_t i = 0; i < 5; ++i)
        ASSERT_EQ(disk[Fixture::physical(128 + i)], expected[i]);
}

TEST(fat32_append_failures_do_not_advance_the_descriptor_offset) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    ASSERT(fd >= 0);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 17, 0), 17L);
    fail_read = ROOT;
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -5L);
    fail_write = ROOT + 2;
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -5L);
    fail_write = ROOT;
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -5L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 17L);
    ASSERT_EQ(vfs_manager::get_fd_node(fd)->length, 128UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

TEST(fat32_partial_append_publishes_only_the_acknowledged_prefix) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    ASSERT(fd >= 0);
    std::array<uint8_t, 700> data{};
    fail_read = ROOT + 3;
    ASSERT_EQ(vfs_manager::sys_write(fd, data.data(), data.size()), 384L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 512L);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 512U);
    for (size_t i = 128; i < 512; ++i)
        ASSERT_EQ(disk[Fixture::physical(i)], 0);
}

TEST(fat32_append_reports_capacity_and_preserves_offset_on_no_space) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY - 10);
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    ASSERT(fd >= 0);
    std::array<uint8_t, 20> data{};
    ASSERT_EQ(vfs_manager::sys_write(fd, data.data(), data.size()), 10L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), static_cast<long>(CAPACITY));
    ASSERT_EQ(vfs_manager::sys_write(fd, "x", 1), -28L);
    ASSERT_EQ(vfs_manager::sys_write(fd, nullptr, 0), 0L);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), static_cast<long>(CAPACITY));
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), CAPACITY);
}

TEST(fat32_reentrant_append_cannot_select_eof_inside_another_append) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int first = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    const int second = vfs_manager::sys_open("/journal.log", 2 | 0x0400);
    ASSERT_EQ(first, 0);
    ASSERT_EQ(second, 1);
    static long result;
    on_read = [] { result = vfs_manager::sys_write(1, "B", 1); };
    ASSERT_EQ(vfs_manager::sys_write(first, "A", 1), 1L);
    ASSERT_EQ(result, -16L);
    ASSERT_EQ(vfs_manager::sys_lseek(second, 0, 1), 0L);
    ASSERT_EQ(vfs_manager::sys_write(second, "B", 1), 1L);
    ASSERT_EQ(disk[Fixture::physical(128)], 'A');
    ASSERT_EQ(disk[Fixture::physical(129)], 'B');
}

TEST(fat32_metadata_read_errors_leave_cached_size_output_and_offset_unchanged) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT(fd >= 0);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 17, 0), 17L);
    struct stat info;
    std::memset(&info, 0xA5, sizeof(info));
    const auto original = info;
    const auto before = disk;
    fail_read = ROOT;
    ASSERT_EQ(vfs_manager::sys_fstat(fd, &info), -5);
    ASSERT(std::memcmp(&info, &original, sizeof(info)) == 0);
    fail_read = ROOT;
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 2), -5L);
    ASSERT_EQ(vfs_manager::get_fd_node(fd)->length, 128UL);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 17L);
    ASSERT(disk == before);
    ASSERT_EQ(write_attempts, 0UL);
    ASSERT_EQ(flush_attempts, 0UL);
}

TEST(fat32_metadata_refresh_rejects_changed_or_malformed_directory_entries) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT(fd >= 0);
    const auto before = disk;
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        disk = before;
        auto* entry = disk.data() + ROOT * SECTOR;
        if (mutation == 0) entry[0] = 0xE5;
        if (mutation == 1) entry[11] = 0x10;
        if (mutation == 2) put16(entry + 26, 4);
        if (mutation == 3) put32(entry + 28, CAPACITY + 1);
        struct stat info{};
        info.st_size = -123;
        ASSERT_EQ(vfs_manager::sys_fstat(fd, &info), -5);
        ASSERT_EQ(info.st_size, -123);
        ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 2), -5L);
        ASSERT_EQ(vfs_manager::get_fd_node(fd)->length, 128UL);
        ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 0L);
    }
}

TEST(fat32_metadata_refresh_reports_contention_through_vfs) {
    Fixture fixture;
    vfs_manager::set_root(fat32::mount(PARTITION));
    const int fd = vfs_manager::sys_open("/journal.log", 2);
    ASSERT_EQ(fd, 0);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 17, 0), 17L);
    static int stat_result;
    static long seek_result;
    static struct stat info;
    info = {};
    info.st_size = -123;
    on_read = [] {
        stat_result = vfs_manager::sys_fstat(0, &info);
        seek_result = vfs_manager::sys_lseek(0, 0, 2);
    };
    ASSERT_EQ(vfs_manager::sys_fsync(fd), 0);
    ASSERT_EQ(stat_result, -16);
    ASSERT_EQ(seek_result, -16L);
    ASSERT_EQ(info.st_size, -123);
    ASSERT_EQ(vfs_manager::sys_lseek(fd, 0, 1), 17L);
}

TEST(fat32_sync_and_truncate_report_contention_without_waiting) {
    Fixture fixture;
    reentrant_file = Fixture::open();
    static int sync_result, truncate_result;
    on_read = [] {
        sync_result = reentrant_file->ops->fsync(reentrant_file);
        truncate_result = reentrant_file->ops->truncate(reentrant_file, 0);
    };
    ASSERT_EQ(reentrant_file->ops->write(reentrant_file, 0, 1, "x"), 1L);
    ASSERT_EQ(sync_result, -16);
    ASSERT_EQ(truncate_result, -16);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

namespace {
// Independent oracle: each touched sector is wholly before/after the operation;
// acknowledged bytes and every byte exposed by growth must contain the new data.
void check_interrupted(const Image& before, size_t old_length, size_t offset, const uint8_t* data, size_t wanted,
                       size_t acknowledged) {
    const size_t count = wanted < CAPACITY - offset ? wanted : CAPACITY - offset;
    const auto length = get32(disk.data() + ROOT * SECTOR + 28);
    ASSERT(acknowledged <= count);
    ASSERT(length >= old_length);
    ASSERT(length <= (old_length > offset + count ? old_length : offset + count));
    if (acknowledged) ASSERT(length >= offset + acknowledged);
    auto after = before;
    for (size_t i = 0; i < count; ++i)
        after[Fixture::physical(offset + i)] = data[i];
    for (size_t sector = 0; sector < CAPACITY; sector += SECTOR) {
        const auto address = Fixture::physical(sector);
        ASSERT(std::memcmp(disk.data() + address, before.data() + address, SECTOR) == 0 ||
               std::memcmp(disk.data() + address, after.data() + address, SECTOR) == 0);
    }
    for (size_t i = 0; i < acknowledged; ++i)
        ASSERT_EQ(disk[Fixture::physical(offset + i)], data[i]);
    for (size_t i = old_length; i < length; ++i)
        ASSERT_EQ(disk[Fixture::physical(i)], data[i - offset]);
    for (size_t i = 0; i < disk.size(); ++i) {
        const bool contents = (i >= (ROOT + 2) * SECTOR && i < (ROOT + 4) * SECTOR) ||
                              (i >= (ROOT + 6) * SECTOR && i < (ROOT + 8) * SECTOR);
        const bool file_size = i >= ROOT * SECTOR + 28 && i < ROOT * SECTOR + 32;
        if (!contents && !file_size) ASSERT_EQ(disk[i], before[i]);
    }
    ASSERT(!invalid_io);
}

size_t crash_case(unsigned code, unsigned depth, size_t cut, bool after) {
    Fixture fixture;
    auto* file = Fixture::open();
    cut_at = cut;
    cut_after = after;
    auto digits = code;
    for (unsigned operation = 0; operation < depth; ++operation) {
        const auto kind = digits % 3;
        digits /= 3;
        const auto before = disk;
        const size_t old_length = file->length;
        const size_t offset = kind == 0 ? 32 : kind == 1 ? old_length : (old_length < 500 ? old_length : 500);
        const size_t wanted = kind == 0 ? 64 : kind == 1 ? 700 : 900;
        std::array<uint8_t, 900> data;
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<uint8_t>(i * 29 + operation * 47 + 101);
        const auto result = file->ops->write(file, offset, wanted, data.data());
        if (result < 0) ASSERT_EQ(result, offline ? -5L : -28L);
        const auto written = result > 0 ? static_cast<size_t>(result) : 0;
        check_interrupted(before, old_length, offset, data.data(), wanted, written);
        if (offline) break;
        ASSERT_EQ(written, wanted < CAPACITY - offset ? wanted : CAPACITY - offset);
    }
    const auto attempts = write_attempts;
    const auto length = get32(disk.data() + ROOT * SECTOR + 28);
    reset_volatile(); // Power loss discards all mount/vnode state, never the disk.
    file = Fixture::open();
    ASSERT_EQ(file->length, static_cast<size_t>(length));
    std::array<uint8_t, CAPACITY> contents{};
    ASSERT_EQ(file->ops->read(file, 0, contents.size(), contents.data()), static_cast<long>(length));
    for (size_t i = 0; i < length; ++i)
        ASSERT_EQ(contents[i], disk[Fixture::physical(i)]);
    return attempts;
}

size_t replay_case(unsigned code, unsigned depth, size_t cut, bool after) {
    try {
        return crash_case(code, depth, cut, after);
    } catch (const std::exception& error) {
        throw rucux_test::AssertionFailure(std::string(error.what()) + "\nReplay: test_fat32 --crash " +
                                           std::to_string(code) + " " + std::to_string(depth) + " " +
                                           std::to_string(cut) + (after ? " after" : " before"));
    }
}

size_t sync_crash_case(unsigned code, unsigned depth, size_t cut, bool after, bool append = false) {
    Fixture fixture;
    vfs_node* file;
    if (append) {
        vfs_manager::set_root(fat32::mount(PARTITION));
        ASSERT_EQ(vfs_manager::sys_open("/journal.log", 2 | 0x0400), 0);
        ASSERT_EQ(vfs_manager::sys_open("/journal.log", 2 | 0x0400), 1);
        file = vfs_manager::get_fd_node(0);
    } else {
        file = Fixture::open();
    }
    cut_at = cut;
    cut_after = after;
    auto digits = code;
    for (unsigned operation = 0; operation < depth; ++operation) {
        const auto kind = digits % 6;
        digits /= 6;
        const auto before = disk;
        const size_t previous_length = get32(disk.data() + ROOT * SECTOR + 28);
        if (kind < 3) {
            const size_t boundary = kind == 0 ? 32 : 500;
            const size_t offset = (kind == 1 || (append && kind == 0))
                                      ? previous_length
                                      : (previous_length < boundary ? previous_length : boundary);
            const size_t wanted = kind == 0 ? 64 : kind == 1 ? 700 : 900;
            std::array<uint8_t, 900> data;
            for (size_t i = 0; i < data.size(); ++i)
                data[i] = static_cast<uint8_t>(i * 29 + operation * 47 + 101);
            size_t written;
            if (append && kind < 2) {
                const auto old_offset = descriptors[kind].offset;
                const auto result = vfs_manager::sys_write(static_cast<int>(kind), data.data(), wanted);
                written = result > 0 ? static_cast<size_t>(result) : 0;
                if (result < 0) ASSERT_EQ(result, offline ? -5L : -28L);
                ASSERT_EQ(descriptors[kind].offset, written ? previous_length + written : old_offset);
            } else {
                const auto result = file->ops->write(file, offset, wanted, data.data());
                if (result < 0) ASSERT_EQ(result, offline ? -5L : -28L);
                written = result > 0 ? static_cast<size_t>(result) : 0;
            }
            check_interrupted(before, previous_length, offset, data.data(), wanted, written);
            if (!offline) ASSERT_EQ(written, wanted < CAPACITY - offset ? wanted : CAPACITY - offset);
        } else if (kind == 5) {
            const int result = file->ops->fsync(file);
            ASSERT_EQ(result, offline ? -5 : 0);
            ASSERT(disk == before);
        } else {
            const size_t length = kind == 3 ? 0 : previous_length / 2;
            const int result = file->ops->truncate(file, length);
            auto resized = before;
            put32(resized.data() + ROOT * SECTOR + 28, static_cast<uint32_t>(length));
            ASSERT(disk == before || disk == resized);
            ASSERT_EQ(result, offline ? -5 : 0);
            if (!result) {
                ASSERT(disk == resized);
                ASSERT_EQ(file->length, length);
            } else {
                ASSERT_EQ(file->length, previous_length);
            }
        }
        if (offline) break;
    }
    ASSERT(!invalid_io);
    const auto attempts = event_attempts;
    const auto length = get32(disk.data() + ROOT * SECTOR + 28);
    reset_volatile();
    file = Fixture::open();
    ASSERT_EQ(file->length, static_cast<size_t>(length));
    std::array<uint8_t, CAPACITY> bytes{};
    ASSERT_EQ(file->ops->read(file, 0, bytes.size(), bytes.data()), static_cast<long>(length));
    for (size_t i = 0; i < length; ++i)
        ASSERT_EQ(bytes[i], disk[Fixture::physical(i)]);
    return attempts;
}

size_t sync_replay_case(unsigned code, unsigned depth, size_t cut, bool after, bool append = false) {
    try {
        return sync_crash_case(code, depth, cut, after, append);
    } catch (const std::exception& error) {
        throw rucux_test::AssertionFailure(std::string(error.what()) + "\nReplay: test_fat32 " +
                                           (append ? "--append-crash " : "--sync-crash ") + std::to_string(code) + " " +
                                           std::to_string(depth) + " " + std::to_string(cut) +
                                           (after ? " after" : " before"));
    }
}
} // namespace

TEST(fat32_bounded_crash_campaign) {
    size_t workloads = 0, cuts = 0;
    unsigned combinations = 1;
    for (unsigned depth = 1; depth <= 3; ++depth) {
        combinations *= 3;
        for (unsigned code = 0; code < combinations; ++code) {
            const auto writes = replay_case(code, depth, SIZE_MAX, false);
            ++workloads;
            for (size_t cut = 0; cut < writes; ++cut) {
                replay_case(code, depth, cut, false);
                replay_case(code, depth, cut, true);
                cuts += 2;
            }
        }
    }
    std::cout << "FAT32 crash campaign: " << workloads << " workloads, " << cuts << " interrupted executions\n";
}

TEST(fat32_truncate_write_fsync_crash_campaign) {
    size_t workloads = 0, cuts = 0;
    unsigned combinations = 1;
    for (unsigned depth = 1; depth <= 3; ++depth) {
        combinations *= 6;
        for (unsigned code = 0; code < combinations; ++code) {
            const auto events = sync_replay_case(code, depth, SIZE_MAX, false);
            ++workloads;
            for (size_t cut = 0; cut < events; ++cut) {
                sync_replay_case(code, depth, cut, false);
                sync_replay_case(code, depth, cut, true);
                cuts += 2;
            }
        }
    }
    std::cout << "FAT32 sync crash campaign: " << workloads << " workloads, " << cuts << " interrupted executions\n";
}

TEST(fat32_append_truncate_fsync_crash_campaign) {
    size_t workloads = 0, cuts = 0;
    unsigned combinations = 1;
    for (unsigned depth = 1; depth <= 3; ++depth) {
        combinations *= 6;
        for (unsigned code = 0; code < combinations; ++code) {
            const auto events = sync_replay_case(code, depth, SIZE_MAX, false, true);
            ++workloads;
            for (size_t cut = 0; cut < events; ++cut) {
                sync_replay_case(code, depth, cut, false, true);
                sync_replay_case(code, depth, cut, true, true);
                cuts += 2;
            }
        }
    }
    std::cout << "FAT32 append crash campaign: " << workloads << " workloads, " << cuts << " interrupted executions\n";
}

int main(int argc, char** argv) {
    if (argc == 6 && (std::strcmp(argv[1], "--crash") == 0 || std::strcmp(argv[1], "--sync-crash") == 0 ||
                      std::strcmp(argv[1], "--append-crash") == 0)) {
        const bool sync = std::strcmp(argv[1], "--crash") != 0;
        const bool append = std::strcmp(argv[1], "--append-crash") == 0;
        char* ends[3]{};
        errno = 0;
        const auto code = std::strtoul(argv[2], &ends[0], 10);
        const auto depth = std::strtoul(argv[3], &ends[1], 10);
        const auto cut = std::strtoul(argv[4], &ends[2], 10);
        unsigned combinations = 1;
        if (depth >= 1 && depth <= 3)
            for (unsigned i = 0; i < depth; ++i)
                combinations *= sync ? 6 : 3;
        if (errno == ERANGE || argv[2][0] < '0' || argv[2][0] > '9' || argv[3][0] < '0' || argv[3][0] > '9' ||
            argv[4][0] < '0' || argv[4][0] > '9' || *ends[0] || *ends[1] || *ends[2] || depth < 1 || depth > 3 ||
            code >= combinations || (cut > 32 && cut != SIZE_MAX) ||
            (std::strcmp(argv[5], "before") && std::strcmp(argv[5], "after")))
            return 2;
        try {
            if (sync)
                sync_replay_case(static_cast<unsigned>(code), static_cast<unsigned>(depth), cut,
                                 std::strcmp(argv[5], "after") == 0, append);
            else
                replay_case(static_cast<unsigned>(code), static_cast<unsigned>(depth), cut,
                            std::strcmp(argv[5], "after") == 0);
            std::cout << "FAT32 crash replay: PASS\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 1) {
        std::cerr << "Usage: test_fat32 [--crash|--sync-crash|--append-crash CODE DEPTH CUT before|after]\n";
        return 2;
    }
    return RUN_ALL_TESTS("FAT32 interrupted writes");
}
