// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/vfs/ext4.hpp>

namespace kernel::vfs::ext4 {

vfs_node* mount(uint32_t partition_lba) noexcept {
    kernel::print("EXT4: Mounting partition at LBA %u\n", partition_lba);
    // Stub implementation for Ext4
    // A full driver requires thousands of lines to handle inodes, block groups,
    // extents, journal (jbd2), directories, and attributes.
    return nullptr;
}

} // namespace kernel::vfs::ext4
