// SPDX-License-Identifier: MIT
// Production FAT32, an atomic-sector disk, and volatile allocations lost on reboot.
#include "test_harness.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <kernel/vfs/ata.hpp>
#include <kernel/vfs/fat32.hpp>

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
size_t cut_at = SIZE_MAX;
bool cut_after;
bool offline;
bool invalid_io;
uint32_t fail_read = UINT32_MAX;
uint32_t fail_write = UINT32_MAX;
void (*on_read)();
vfs_node* reentrant_file;
size_t nested_result;

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
    for (size_t i = 0; i < allocation_count; ++i)
        std::free(allocations[i]);
    allocation_count = 0;
    allocation_limit = SIZE_MAX;
    write_attempts = 0;
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
        auto* file = root->ops->finddir(root, "journal.log");
        ASSERT(file);
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
    const bool interrupted = write_attempts++ == cut_at;
    if (!interrupted || cut_after) std::memcpy(disk.data() + lba * SECTOR, buffer, SECTOR);
    if (interrupted) offline = true;
    return !offline;
}
} // namespace vfs::ata
} // namespace kernel

TEST(fat32_stops_at_first_data_read_failure) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY);
    auto* file = Fixture::open();
    std::array<uint8_t, 1200> data{};
    fail_read = ROOT + 3;
    ASSERT_EQ(file->ops->write(file, 0, data.size(), data.data()), 512UL);
    ASSERT_EQ(write_attempts, 1UL);
}

TEST(fat32_stops_at_first_data_write_failure) {
    Fixture fixture;
    put32(disk.data() + ROOT * SECTOR + 28, CAPACITY);
    auto* file = Fixture::open();
    std::array<uint8_t, 1200> data{};
    fail_write = ROOT + 3;
    ASSERT_EQ(file->ops->write(file, 0, data.size(), data.data()), 512UL);
    ASSERT_EQ(write_attempts, 1UL);
}

TEST(fat32_does_not_acknowledge_growth_after_metadata_read_failure) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, 700> data{};
    fail_read = ROOT;
    ASSERT_EQ(file->ops->write(file, 128, data.size(), data.data()), 0UL);
    ASSERT_EQ(file->length, 128UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

TEST(fat32_does_not_acknowledge_growth_after_metadata_write_failure) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, 700> data{};
    fail_write = ROOT;
    ASSERT_EQ(file->ops->write(file, 96, data.size(), data.data()), 32UL);
    ASSERT_EQ(file->length, 128UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 128U);
}

TEST(fat32_read_clamps_overflowing_lengths) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, CAPACITY> buffer{};
    ASSERT_EQ(file->ops->read(file, 64, SIZE_MAX, buffer.data()), 64UL);
    for (size_t i = 0; i < 64; ++i)
        ASSERT_EQ(buffer[i], disk[Fixture::physical(i + 64)]);
    ASSERT_EQ(file->ops->read(file, SIZE_MAX, 1, buffer.data()), 0UL);
}

TEST(fat32_rejects_holes_and_short_writes_at_allocated_capacity) {
    Fixture fixture;
    auto* file = Fixture::open();
    std::array<uint8_t, CAPACITY * 2> buffer{};
    ASSERT_EQ(file->ops->write(file, 129, 1, buffer.data()), 0UL);
    ASSERT_EQ(write_attempts, 0UL);
    ASSERT_EQ(file->ops->write(file, SIZE_MAX, 1, buffer.data()), 0UL);
    ASSERT_EQ(file->ops->write(file, 0, 0, nullptr), 0UL);
    ASSERT_EQ(file->ops->write(file, 128, buffer.size(), buffer.data()), CAPACITY - 128);
    ASSERT_EQ(file->length, CAPACITY);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), CAPACITY);
    ASSERT_EQ(file->ops->write(file, CAPACITY, 1, buffer.data()), 0UL);
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
        ASSERT(!root->ops->finddir(root, "journal.log"));
        ASSERT(!invalid_io);
    }
    Fixture fixture;
    put32(disk.data() + (PARTITION + 2) * SECTOR + 8, 2);
    ASSERT(!fat32::mount(PARTITION));
    ASSERT_EQ(allocation_count, 0UL);
}

TEST(fat32_refreshes_stale_handles_and_preserves_logical_reset_behavior) {
    Fixture fixture;
    auto* first = Fixture::open();
    auto* second = Fixture::open();
    std::array<uint8_t, 700> buffer{};
    ASSERT_EQ(first->ops->write(first, 128, buffer.size(), buffer.data()), buffer.size());
    ASSERT_EQ(second->ops->write(second, 0, 16, buffer.data()), 16UL);
    ASSERT_EQ(second->length, 828UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 828U);
    second->length = 0; // Existing VFS ftruncate changes the logical vnode length.
    ASSERT_EQ(second->ops->write(second, 0, 5, buffer.data()), 5UL);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 28), 5U);
    ASSERT_EQ(get32(disk.data() + ROOT * SECTOR + 60), 512U);
}

TEST(fat32_reentrant_io_fails_without_interleaving_disk_commands) {
    Fixture fixture;
    reentrant_file = Fixture::open();
    nested_result = SIZE_MAX;
    on_read = [] { nested_result = reentrant_file->ops->write(reentrant_file, 0, 1, "x"); };
    ASSERT_EQ(reentrant_file->ops->write(reentrant_file, 0, 1, "y"), 1UL);
    ASSERT_EQ(nested_result, 0UL);
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
        ASSERT(!root->ops->finddir(root, "journal.log"));
        ASSERT_EQ(allocation_count, baseline);
    }
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
        const auto written = file->ops->write(file, offset, wanted, data.data());
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
    ASSERT_EQ(file->ops->read(file, 0, contents.size(), contents.data()), static_cast<size_t>(length));
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

int main(int argc, char** argv) {
    if (argc == 6 && std::strcmp(argv[1], "--crash") == 0) {
        char* ends[3]{};
        errno = 0;
        const auto code = std::strtoul(argv[2], &ends[0], 10);
        const auto depth = std::strtoul(argv[3], &ends[1], 10);
        const auto cut = std::strtoul(argv[4], &ends[2], 10);
        if (errno == ERANGE || argv[2][0] < '0' || argv[2][0] > '9' || argv[3][0] < '0' || argv[3][0] > '9' ||
            argv[4][0] < '0' || argv[4][0] > '9' || *ends[0] || *ends[1] || *ends[2] || depth < 1 || depth > 3 ||
            code >= (depth == 1   ? 3UL
                     : depth == 2 ? 9UL
                                  : 27UL) ||
            (cut > 32 && cut != SIZE_MAX) || (std::strcmp(argv[5], "before") && std::strcmp(argv[5], "after")))
            return 2;
        try {
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
        std::cerr << "Usage: test_fat32 [--crash CODE DEPTH CUT before|after]\n";
        return 2;
    }
    return RUN_ALL_TESTS("FAT32 interrupted writes");
}
