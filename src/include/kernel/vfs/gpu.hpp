// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/vfs/vfs.hpp>

namespace kernel::vfs::gpu {

// Creates the /dev/gpu0 node
vfs_node* create() noexcept;

// Expose the backbuffer for copying from userspace
uint8_t* get_backbuffer() noexcept;

} // namespace kernel::vfs::gpu
