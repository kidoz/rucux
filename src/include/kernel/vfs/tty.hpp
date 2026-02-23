// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/vfs/vfs.hpp>
#include <stdint.h>

namespace kernel::vfs::tty {

vfs_node* create() noexcept;

// A way for the KBD driver to pump raw ASCII into the TTY line discipline
void feed_input(char c) noexcept;

} // namespace kernel::vfs::tty
