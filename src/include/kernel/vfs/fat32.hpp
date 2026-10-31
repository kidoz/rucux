// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/vfs/vfs.hpp>
#include <stdint.h>

namespace kernel::vfs::fat32 {

// Mount a 512-byte-sector FAT32 layout on the primary ATA disk. This driver
// reads short (8.3) names and overwrites/extends files only within existing FAT
// chains; it does not allocate clusters or support sparse writes/transactions.
// A successful write acknowledges a contiguous prefix, publishing growth only
// after the directory size write succeeds. I/O failure may persist additional
// unacknowledged bytes. Durability assumes ordered atomic sectors and a device
// that honors successful ATA cache flushes; torn sectors are not covered.
// Operations reserve the device without waiting; contention returns 0/null via
// the existing read/write callbacks; truncate/fsync return negative errno.
// Truncate durably shrinks EOF while retaining the chain. Growth via truncate
// returns -EOPNOTSUPP. Fsync explicitly checks device flush completion. These
// remain a bounded profile, not full POSIX filesystem semantics. Successful
// mount/vnode objects remain owned by the VFS.
vfs_node* mount(uint32_t partition_lba) noexcept;

} // namespace kernel::vfs::fat32
