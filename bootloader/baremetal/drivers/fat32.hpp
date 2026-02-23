// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace fs::fat32 {

struct file_info {
    uint32_t start_cluster;
    uint32_t size_bytes;
};

// Initialize the FAT32 filesystem given the starting LBA of the partition
bool init(uint32_t partition_lba);

// Find a file by its 11-character DOS name (e.g., "KERNEL  ELF")
bool find_file(const char* name, file_info& out_info);

// Read the contents of a file into the provided buffer
bool read_file(const file_info& info, uint8_t* buffer);

} // namespace fs::fat32
