// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <uapi/kernel/log.h>

namespace kernel::vfs {
struct vfs_node;
}

namespace kernel::log {

void init() noexcept;
void capture_console_char(char c) noexcept;
kernel::vfs::vfs_node* create_kmsg_node() noexcept;
long sys_log_ctl(uint32_t op, void* arg0, size_t arg1, uintptr_t arg2) noexcept;

} // namespace kernel::log
