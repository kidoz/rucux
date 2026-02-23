// SPDX-License-Identifier: MIT
#include "fat32.hpp"
#include "sd.hpp"

namespace fs::fat32 {

static uint32_t g_partition_lba = 0;
static uint32_t g_bytes_per_sector = 0;
static uint8_t g_sectors_per_cluster = 0;
static uint16_t g_reserved_sectors = 0;
static uint8_t g_num_fats = 0;
static uint32_t g_sectors_per_fat = 0;
static uint32_t g_root_cluster = 0;

static uint32_t g_fat_lba = 0;
static uint32_t g_data_lba = 0;

static uint8_t g_sector_buffer[512];

bool init(uint32_t partition_lba) {
    g_partition_lba = partition_lba;

    if (!hw::sd::read_block(partition_lba, g_sector_buffer)) {
        return false;
    }

    g_bytes_per_sector = *reinterpret_cast<uint16_t*>(&g_sector_buffer[11]);
    g_sectors_per_cluster = g_sector_buffer[13];
    g_reserved_sectors = *reinterpret_cast<uint16_t*>(&g_sector_buffer[14]);
    g_num_fats = g_sector_buffer[16];
    g_sectors_per_fat = *reinterpret_cast<uint32_t*>(&g_sector_buffer[36]);
    g_root_cluster = *reinterpret_cast<uint32_t*>(&g_sector_buffer[44]);

    if (g_bytes_per_sector != 512) {
        return false; // We only support 512-byte sectors for simplicity
    }

    g_fat_lba = g_partition_lba + g_reserved_sectors;
    g_data_lba = g_fat_lba + (g_num_fats * g_sectors_per_fat);

    return true;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    if (cluster < 2) return 0;
    return g_data_lba + ((cluster - 2) * g_sectors_per_cluster);
}

static uint32_t get_next_cluster(uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_fat_lba + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t temp_buf[512];
    if (!hw::sd::read_block(fat_sector, temp_buf)) {
        return 0x0FFFFFFF;
    }

    uint32_t next = *reinterpret_cast<uint32_t*>(&temp_buf[ent_offset]) & 0x0FFFFFFF;
    return next;
}

static bool match_name(const uint8_t* entry_name, const char* target_name) {
    for (int i = 0; i < 11; ++i) {
        if (entry_name[i] != target_name[i]) return false;
    }
    return true;
}

bool find_file(const char* name, file_info& out_info) {
    uint32_t current_cluster = g_root_cluster;

    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(current_cluster);

        for (uint8_t sec = 0; sec < g_sectors_per_cluster; ++sec) {
            if (!hw::sd::read_block(lba + sec, g_sector_buffer)) {
                return false;
            }

            for (uint32_t offset = 0; offset < 512; offset += 32) {
                uint8_t* entry = &g_sector_buffer[offset];

                if (entry[0] == 0x00) {
                    return false; // End of directory
                }
                if (entry[0] == 0xE5) {
                    continue; // Deleted entry
                }
                if (entry[11] == 0x0F) {
                    continue; // Long file name
                }
                if (entry[11] & 0x08) {
                    continue; // Volume label
                }

                if (match_name(entry, name)) {
                    out_info.start_cluster =
                        (static_cast<uint32_t>(entry[21]) << 24) | (static_cast<uint32_t>(entry[20]) << 16) |
                        (static_cast<uint32_t>(entry[27]) << 8) | (static_cast<uint32_t>(entry[26]));
                    out_info.size_bytes = *reinterpret_cast<uint32_t*>(&entry[28]);
                    return true;
                }
            }
        }
        current_cluster = get_next_cluster(current_cluster);
    }
    return false;
}

bool read_file(const file_info& info, uint8_t* buffer) {
    uint32_t current_cluster = info.start_cluster;
    uint32_t bytes_read = 0;
    uint8_t* ptr = buffer;

    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF8 && bytes_read < info.size_bytes) {
        uint32_t lba = cluster_to_lba(current_cluster);

        for (uint8_t sec = 0; sec < g_sectors_per_cluster; ++sec) {
            if (bytes_read >= info.size_bytes) break;

            if (!hw::sd::read_block(lba + sec, g_sector_buffer)) {
                return false;
            }

            uint32_t to_copy = 512;
            if (info.size_bytes - bytes_read < 512) {
                to_copy = info.size_bytes - bytes_read;
            }

            for (uint32_t i = 0; i < to_copy; i++) {
                ptr[i] = g_sector_buffer[i];
            }
            ptr += to_copy;
            bytes_read += to_copy;
        }
        current_cluster = get_next_cluster(current_cluster);
    }
    return true;
}

} // namespace fs::fat32
