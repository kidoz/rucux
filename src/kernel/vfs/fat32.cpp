// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/print.hpp>
#include <kernel/vfs/ata.hpp>
#include <kernel/vfs/fat32.hpp>
#include <lib/string.hpp>
#include <knew.hpp>

namespace kernel::vfs::fat32 {

struct fat32_info {
    uint32_t partition_lba;
    uint32_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;

    uint32_t fat_lba;
    uint32_t data_lba;
};

struct fat32_node_internal {
    vfs_node* vnode;
    fat32_info* info;
    uint32_t start_cluster;
};

static uint32_t cluster_to_lba(fat32_info* info, uint32_t cluster) {
    if (cluster < 2) return 0;
    return info->data_lba + ((cluster - 2) * info->sectors_per_cluster);
}

static uint32_t get_next_cluster(fat32_info* info, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = info->fat_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t temp_buf[512];
    if (!ata::read_sectors(fat_sector, 1, temp_buf)) {
        return 0x0FFFFFFF;
    }

    uint32_t next = *reinterpret_cast<uint32_t*>(&temp_buf[ent_offset]) & 0x0FFFFFFF;
    return next;
}

static size_t fat32_read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept {
    fat32_node_internal* internal = reinterpret_cast<fat32_node_internal*>(node->ptr);
    fat32_info* info = internal->info;

    if (offset >= node->length) return 0;
    if (offset + size > node->length) size = node->length - offset;

    uint32_t cluster = internal->start_cluster;
    uint32_t cluster_size = info->sectors_per_cluster * info->bytes_per_sector;

    // Skip clusters to reach offset
    size_t skip_clusters = offset / cluster_size;
    for (size_t i = 0; i < skip_clusters; i++) {
        cluster = get_next_cluster(info, cluster);
        if (cluster >= 0x0FFFFFF8) return 0;
    }

    size_t cluster_offset = offset % cluster_size;
    size_t bytes_read = 0;
    uint8_t* ptr = static_cast<uint8_t*>(buffer);

    uint8_t sec_buf[512];

    while (bytes_read < size && cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(info, cluster);

        for (uint8_t sec = 0; sec < info->sectors_per_cluster; ++sec) {
            if (bytes_read >= size) break;

            // Skip sectors within the first cluster if offset dictates
            if (cluster_offset >= 512) {
                cluster_offset -= 512;
                continue;
            }

            if (!ata::read_sectors(lba + sec, 1, sec_buf)) {
                return bytes_read;
            }

            size_t to_copy = 512 - cluster_offset;
            if (size - bytes_read < to_copy) {
                to_copy = size - bytes_read;
            }

            lib::memcpy(ptr + bytes_read, sec_buf + cluster_offset, to_copy);
            bytes_read += to_copy;
            cluster_offset = 0; // Only applies to first sector read
        }

        cluster = get_next_cluster(info, cluster);
    }

    return bytes_read;
}

static vfs_node* fat32_finddir(vfs_node* node, const char* name) noexcept {
    fat32_node_internal* internal = reinterpret_cast<fat32_node_internal*>(node->ptr);
    fat32_info* info = internal->info;

    uint32_t current_cluster = internal->start_cluster;
    uint8_t sec_buf[512];

    // Extremely simplistic 8.3 filename parsing matching exactly "FILENAMEEXT"
    char target_name[11];
    lib::memset(target_name, ' ', 11);

    int j = 0;
    for (int i = 0; i < 8 && name[i] && name[i] != '.'; i++, j++) {
        target_name[j] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]; // UPPERCASE
    }

    const char* ext = nullptr;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') {
            ext = &name[i + 1];
            break;
        }
    }

    if (ext) {
        j = 8;
        for (int i = 0; i < 3 && ext[i]; i++, j++) {
            target_name[j] = ext[i] >= 'a' && ext[i] <= 'z' ? ext[i] - 32 : ext[i]; // UPPERCASE
        }
    }

    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(info, current_cluster);

        for (uint8_t sec = 0; sec < info->sectors_per_cluster; ++sec) {
            if (!ata::read_sectors(lba + sec, 1, sec_buf)) {
                return nullptr;
            }

            for (uint32_t offset = 0; offset < 512; offset += 32) {
                uint8_t* entry = &sec_buf[offset];

                if (entry[0] == 0x00) return nullptr; // End of directory
                if (entry[0] == 0xE5) continue;       // Deleted
                if (entry[11] == 0x0F) continue;      // LFN
                if (entry[11] & 0x08) continue;       // Volume Label

                bool match = true;
                for (int i = 0; i < 11; i++) {
                    if (entry[i] != target_name[i]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    vfs_node* v = new vfs_node();
                    size_t nlen = lib::strlen(name);
                    char* ndup = new char[nlen + 1];
                    lib::memcpy(ndup, name, nlen + 1);
                    v->name = ndup;
                    v->name_hash = vfs_node::hash_name(name);
                    v->length = *reinterpret_cast<uint32_t*>(&entry[28]);
                    v->type = (entry[11] & 0x10) ? file_type::DIRECTORY : file_type::REGULAR;

                    v->ops = new vfs_ops();
                    v->ops->read = fat32_read;
                    v->ops->write = nullptr;
                    v->ops->open = nullptr;
                    v->ops->close = nullptr;
                    v->ops->ioctl = nullptr;
                    v->ops->readdir = nullptr;
                    v->ops->finddir = (v->type == file_type::DIRECTORY) ? fat32_finddir : nullptr;
                    v->ops->mmap = nullptr;
                    v->ops->poll = nullptr;

                    fat32_node_internal* new_internal = new fat32_node_internal();
                    new_internal->vnode = v;
                    new_internal->info = info;
                    new_internal->start_cluster =
                        (static_cast<uint32_t>(entry[21]) << 24) | (static_cast<uint32_t>(entry[20]) << 16) |
                        (static_cast<uint32_t>(entry[27]) << 8) | (static_cast<uint32_t>(entry[26]));
                    v->ptr = reinterpret_cast<vfs_node*>(new_internal);
                    return v;
                }
            }
        }
        current_cluster = get_next_cluster(info, current_cluster);
    }
    return nullptr;
}

vfs_node* mount(uint32_t partition_lba) noexcept {
    uint8_t buf[512];
    if (!ata::read_sectors(partition_lba, 1, buf)) {
        kernel::print("FAT32: Failed to read VBR.\n");
        return nullptr;
    }

    fat32_info* info = new fat32_info();
    info->partition_lba = partition_lba;
    info->bytes_per_sector = *reinterpret_cast<uint16_t*>(&buf[11]);
    info->sectors_per_cluster = buf[13];
    info->reserved_sectors = *reinterpret_cast<uint16_t*>(&buf[14]);
    info->num_fats = buf[16];
    info->sectors_per_fat = *reinterpret_cast<uint32_t*>(&buf[36]);
    info->root_cluster = *reinterpret_cast<uint32_t*>(&buf[44]);

    if (info->bytes_per_sector != 512) {
        kernel::print("FAT32: Unsupported sector size: {}\n", info->bytes_per_sector);
        return nullptr;
    }

    info->fat_lba = info->partition_lba + info->reserved_sectors;
    info->data_lba = info->fat_lba + (info->num_fats * info->sectors_per_fat);

    vfs_node* root = new vfs_node();
    root->name = "/";
    root->name_hash = vfs_node::hash_name("/");
    root->type = file_type::DIRECTORY;

    root->ops = new vfs_ops();
    root->ops->read = nullptr;
    root->ops->write = nullptr;
    root->ops->open = nullptr;
    root->ops->close = nullptr;
    root->ops->ioctl = nullptr;
    root->ops->readdir = nullptr;
    root->ops->finddir = fat32_finddir;
    root->ops->mmap = nullptr;
    root->ops->poll = nullptr;

    fat32_node_internal* internal = new fat32_node_internal();
    internal->vnode = root;
    internal->info = info;
    internal->start_cluster = info->root_cluster;

    root->ptr = reinterpret_cast<vfs_node*>(internal);

    kernel::print("FAT32 Mounted at LBA {}\n", partition_lba);
    return root;
}

} // namespace kernel::vfs::fat32
