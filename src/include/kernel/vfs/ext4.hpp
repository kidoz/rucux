// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/vfs/vfs.hpp>
#include <stdint.h>

namespace kernel::vfs::ext4 {

// Mounts an Ext4 partition from the primary ATA drive and returns its root node
vfs_node* mount(uint32_t partition_lba) noexcept;

} // namespace kernel::vfs::ext4
