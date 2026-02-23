// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/vfs/vfs.hpp>
#include <stdint.h>

namespace kernel::vfs::fat32 {

// Mounts a FAT32 partition from the primary ATA drive and returns its root node
vfs_node* mount(uint32_t partition_lba) noexcept;

} // namespace kernel::vfs::fat32
