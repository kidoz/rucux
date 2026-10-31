// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/print.hpp>
#include <kernel/sync/atomic.hpp>
#include <kernel/vfs/ata.hpp>
#include <kernel/vfs/fat32.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::vfs::fat32 {
namespace {

constexpr uint32_t SECTOR_SIZE = 512;
constexpr uint32_t END_OF_CHAIN = 0x0FFFFFF8;
struct fat32_info {
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t cluster_count;
    uint8_t sectors_per_cluster;
};
struct fat32_node_internal {
    fat32_info* info;
    uint64_t allocated_bytes;
    size_t observed_length;
    uint32_t start_cluster;
    uint32_t directory_lba;
    uint16_t directory_offset;
};

// The primary ATA disk is shared by all mounts. A reservation cannot sleep or
// spin across device I/O: a concurrent/reentrant operation reports failure.
// This also keeps directory-sector read/modify/write updates from racing.
atomic<bool> g_io_busy{false};
struct operation_guard {
    bool acquired;
    operation_guard() noexcept {
        bool expected = false;
        acquired = g_io_busy.compare_exchange_strong(expected, true, acquire, relaxed);
    }
    ~operation_guard() noexcept {
        if (acquired) g_io_busy.store(false, release);
    }
    operation_guard(const operation_guard&) = delete;
    operation_guard& operator=(const operation_guard&) = delete;
};

uint16_t read16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (static_cast<uint32_t>(p[1]) << 8U));
}
uint32_t read32(const uint8_t* p) noexcept {
    return p[0] | (static_cast<uint32_t>(p[1]) << 8U) | (static_cast<uint32_t>(p[2]) << 16U) |
           (static_cast<uint32_t>(p[3]) << 24U);
}
void write32(uint8_t* p, uint32_t value) noexcept {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(value >> (i * 8U));
}
bool valid_cluster(const fat32_info& info, uint32_t cluster) noexcept {
    return cluster >= 2 && cluster - 2 < info.cluster_count;
}
uint32_t cluster_to_lba(const fat32_info& info, uint32_t cluster) noexcept {
    return info.data_lba + (cluster - 2) * info.sectors_per_cluster;
}
bool next_cluster(const fat32_info& info, uint32_t cluster, uint32_t& next) noexcept {
    if (!valid_cluster(info, cluster)) return false;
    const uint32_t offset = cluster * 4;
    uint8_t sector[SECTOR_SIZE];
    if (!ata::read_sectors(info.fat_lba + offset / SECTOR_SIZE, 1, sector)) return false;
    next = read32(sector + offset % SECTOR_SIZE) & 0x0FFFFFFFU;
    if (next >= END_OF_CHAIN) {
        next = 0;
        return true;
    }
    return valid_cluster(info, next);
}

// This driver never changes FAT chains, so validate/count once at lookup.
// A corrupt/cyclic chain cannot create an unbounded I/O loop or fake capacity.
bool chain_capacity(const fat32_info& info, uint32_t start, uint64_t& bytes) noexcept {
    bytes = 0;
    uint32_t cluster = start;
    for (uint32_t visited = 0; cluster && visited < info.cluster_count; ++visited) {
        if (!valid_cluster(info, cluster)) return false;
        bytes += static_cast<uint64_t>(info.sectors_per_cluster) * SECTOR_SIZE;
        if (!next_cluster(info, cluster, cluster)) return false;
    }
    return cluster == 0;
}

template <typename T>
T* allocate() noexcept {
    void* storage = kmalloc(sizeof(T));
    return storage ? new (storage) T{} : nullptr;
}

size_t fat32_read(vfs_node*, size_t, size_t, void*) noexcept;
size_t fat32_write(vfs_node*, size_t, size_t, const void*) noexcept;
vfs_node* fat32_finddir(vfs_node*, const char*) noexcept;

vfs_node* make_node(fat32_info* info, uint32_t cluster, uint64_t capacity, uint32_t length, file_type type,
                    const char* name, uint32_t directory_lba = 0, uint16_t directory_offset = 0) noexcept {
    auto* node = allocate<vfs_node>();
    auto* ops = allocate<vfs_ops>();
    auto* internal = allocate<fat32_node_internal>();
    const auto name_size = lib::strlen(name) + 1;
    auto* owned_name = static_cast<char*>(kmalloc(name_size));
    if (!node || !ops || !internal || !owned_name) {
        kfree(owned_name);
        kfree(internal);
        kfree(ops);
        kfree(node);
        return nullptr;
    }
    lib::memcpy(owned_name, name, name_size);
    node->name = owned_name;
    node->name_hash = vfs_node::hash_name(name);
    node->length = length;
    node->type = type;
    node->ops = ops;
    node->ptr = reinterpret_cast<vfs_node*>(internal);
    *internal = {info, capacity, length, cluster, directory_lba, directory_offset};
    if (type == file_type::DIRECTORY) {
        ops->finddir = fat32_finddir;
    } else {
        ops->read = fat32_read;
        ops->write = fat32_write;
    }
    return node;
}

// Return immediately on the first error. Later sectors/clusters must never be
// substituted for an unread/unwritten part of the requested contiguous prefix.
size_t transfer(fat32_node_internal& internal, size_t offset, size_t size, void* buffer, bool writing) noexcept {
    const auto& info = *internal.info;
    const uint32_t cluster_size = info.sectors_per_cluster * SECTOR_SIZE;
    uint32_t cluster = internal.start_cluster;
    const size_t skip = offset / cluster_size;
    if (skip >= info.cluster_count) return 0;
    for (size_t i = 0; i < skip; ++i)
        if (!next_cluster(info, cluster, cluster) || !cluster) return 0;
    size_t cluster_offset = offset % cluster_size;
    size_t done = 0;
    uint8_t sector[SECTOR_SIZE];
    auto* bytes = static_cast<uint8_t*>(buffer);
    for (size_t visited = skip; done < size && valid_cluster(info, cluster) && visited < info.cluster_count;
         ++visited) {
        const auto lba = cluster_to_lba(info, cluster);
        for (uint32_t sec = static_cast<uint32_t>(cluster_offset / SECTOR_SIZE);
             sec < info.sectors_per_cluster && done < size; ++sec) {
            const size_t sector_offset = cluster_offset % SECTOR_SIZE;
            if (!ata::read_sectors(lba + sec, 1, sector)) return done;
            const size_t room = SECTOR_SIZE - sector_offset;
            const size_t count = size - done < room ? size - done : room;
            if (writing) {
                lib::memcpy(sector + sector_offset, bytes + done, count);
                if (!ata::write_sectors(lba + sec, 1, sector)) return done;
            } else {
                lib::memcpy(bytes + done, sector + sector_offset, count);
            }
            done += count;
            cluster_offset = 0;
        }
        if (done == size || !next_cluster(info, cluster, cluster)) break;
    }
    return done;
}

size_t fat32_read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept {
    operation_guard guard;
    if (!guard.acquired || !node || node->type != file_type::REGULAR || (!buffer && size) || !node->ptr) return 0;
    auto* internal = reinterpret_cast<fat32_node_internal*>(node->ptr);
    if (offset >= node->length || node->length > internal->allocated_bytes) return 0;
    if (size > node->length - offset) size = node->length - offset;
    return transfer(*internal, offset, size, buffer, false);
}

size_t fat32_write(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept {
    operation_guard guard;
    if (!guard.acquired || !node || node->type != file_type::REGULAR || !node->ptr || !buffer || !size ||
        offset > UINT32_MAX || size > UINT32_MAX - offset)
        return 0;
    auto* internal = reinterpret_cast<fat32_node_internal*>(node->ptr);
    if (!internal->directory_lba || internal->directory_offset > SECTOR_SIZE - 32 ||
        offset >= internal->allocated_bytes)
        return 0;

    uint8_t directory[SECTOR_SIZE];
    if (!ata::read_sectors(internal->directory_lba, 1, directory)) return 0;
    auto* entry = directory + internal->directory_offset;
    const uint32_t disk_length = read32(entry + 28);
    const uint32_t disk_cluster =
        ((static_cast<uint32_t>(read16(entry + 20)) << 16U) | read16(entry + 26)) & 0x0FFFFFFFU;
    if (entry[0] == 0 || entry[0] == 0xE5 || (entry[11] & 0x18U) || disk_cluster != internal->start_cluster ||
        disk_length > internal->allocated_bytes)
        return 0;
    // A second open may hold an older vnode. Refresh it unless the caller has
    // explicitly shortened its logical length (the existing ftruncate path).
    if (node->length == internal->observed_length) {
        node->length = disk_length;
        internal->observed_length = disk_length;
    }
    if (offset > node->length || node->length > internal->allocated_bytes) return 0; // No sparse writes.
    const auto available = internal->allocated_bytes - offset;
    if (size > available) size = static_cast<size_t>(available);
    const size_t old_length = node->length;
    const auto done = transfer(*internal, offset, size, const_cast<void*>(buffer), true);
    if (!done || offset + done <= old_length) return done;

    // Data sectors have completed their flush before publishing the new EOF.
    // A failed size write may itself have persisted: never acknowledge its tail.
    write32(entry + 28, static_cast<uint32_t>(offset + done));
    if (!ata::write_sectors(internal->directory_lba, 1, directory)) return old_length - offset;
    node->length = offset + done;
    internal->observed_length = node->length;
    return done;
}

bool short_name(const char* name, uint8_t (&target)[11]) noexcept {
    if (!name || !*name) return false;
    lib::memset(target, ' ', sizeof(target));
    size_t base = 0, extension = 0;
    bool dot = false;
    for (size_t i = 0; i < 13; ++i) {
        const auto c = static_cast<uint8_t>(name[i]);
        if (!c) return base != 0;
        if (c == '/') return false;
        if (c == '.') {
            if (!base || dot) return false;
            dot = true;
            continue;
        }
        if ((!dot && base == 8) || (dot && extension == 3)) return false;
        const auto index = dot ? 8 + extension++ : base++;
        target[index] = c >= 'a' && c <= 'z' ? static_cast<uint8_t>(c - 'a' + 'A') : c;
    }
    return false;
}

vfs_node* fat32_finddir(vfs_node* node, const char* name) noexcept {
    operation_guard guard;
    uint8_t target[11];
    if (!guard.acquired || !node || node->type != file_type::DIRECTORY || !node->ptr || !short_name(name, target))
        return nullptr;
    auto* internal = reinterpret_cast<fat32_node_internal*>(node->ptr);
    auto* info = internal->info;
    auto cluster = internal->start_cluster;
    uint8_t sector[SECTOR_SIZE];
    for (uint32_t visited = 0; valid_cluster(*info, cluster) && visited < info->cluster_count; ++visited) {
        const auto lba = cluster_to_lba(*info, cluster);
        for (uint32_t sec = 0; sec < info->sectors_per_cluster; ++sec) {
            if (!ata::read_sectors(lba + sec, 1, sector)) return nullptr;
            for (uint16_t offset = 0; offset < SECTOR_SIZE; offset += 32) {
                const auto* entry = sector + offset;
                if (!entry[0]) return nullptr;
                if (entry[0] == 0xE5 || entry[11] == 0x0F || (entry[11] & 0x08U)) continue;
                if (lib::memcmp(entry, target, 11) != 0) continue;
                const auto start =
                    ((static_cast<uint32_t>(read16(entry + 20)) << 16U) | read16(entry + 26)) & 0x0FFFFFFFU;
                const auto type = entry[11] & 0x10U ? file_type::DIRECTORY : file_type::REGULAR;
                const auto length = read32(entry + 28);
                uint64_t capacity;
                if ((!start && (type == file_type::DIRECTORY || length)) || !chain_capacity(*info, start, capacity) ||
                    length > capacity)
                    return nullptr;
                return make_node(info, start, capacity, length, type, name, lba + sec, offset);
            }
        }
        if (!next_cluster(*info, cluster, cluster)) return nullptr;
    }
    return nullptr;
}

} // namespace

vfs_node* mount(uint32_t partition_lba) noexcept {
    operation_guard guard;
    if (!guard.acquired) return nullptr;
    uint8_t bpb[SECTOR_SIZE];
    if (!ata::read_sectors(partition_lba, 1, bpb)) return nullptr;

    const uint32_t sectors_per_cluster = bpb[13];
    const uint32_t reserved = read16(bpb + 14);
    const uint32_t fats = bpb[16];
    const uint32_t sectors_per_fat = read32(bpb + 36);
    const uint32_t total = read16(bpb + 19) ? read16(bpb + 19) : read32(bpb + 32);
    const uint64_t metadata = reserved + static_cast<uint64_t>(fats) * sectors_per_fat;
    const auto fat_flags = read16(bpb + 40);
    if (bpb[510] != 0x55 || bpb[511] != 0xAA || read16(bpb + 11) != SECTOR_SIZE || !sectors_per_cluster ||
        sectors_per_cluster > 128 || (sectors_per_cluster & (sectors_per_cluster - 1)) || !reserved || !fats ||
        fats > 2 || !sectors_per_fat || metadata >= total || total > UINT32_MAX - partition_lba || read16(bpb + 17) ||
        read16(bpb + 22) || read16(bpb + 42) || ((fat_flags & 0x80U) && (fat_flags & 0x0FU)))
        return nullptr;

    fat32_info parsed{};
    parsed.cluster_count = static_cast<uint32_t>((total - metadata) / sectors_per_cluster);
    parsed.sectors_per_cluster = static_cast<uint8_t>(sectors_per_cluster);
    parsed.fat_lba = partition_lba + reserved;
    parsed.data_lba = static_cast<uint32_t>(partition_lba + metadata);
    if (!parsed.cluster_count || parsed.cluster_count + 1 >= 0x0FFFFFF0U ||
        static_cast<uint64_t>(parsed.cluster_count) + 2 > static_cast<uint64_t>(sectors_per_fat) * (SECTOR_SIZE / 4))
        return nullptr;
    const auto root_cluster = read32(bpb + 44) & 0x0FFFFFFFU;
    uint64_t capacity;
    if (!valid_cluster(parsed, root_cluster) || !chain_capacity(parsed, root_cluster, capacity)) return nullptr;
    auto* info = allocate<fat32_info>();
    if (!info) return nullptr;
    *info = parsed;
    auto* root = make_node(info, root_cluster, capacity, 0, file_type::DIRECTORY, "/");
    if (!root) {
        kfree(info);
        return nullptr;
    }
    kernel::print("FAT32 Mounted at LBA {}\n", partition_lba);
    return root;
}

} // namespace kernel::vfs::fat32
