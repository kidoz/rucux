// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::process {

long spawn_path(const char* path, uint32_t requested_tid = 0) noexcept;
long sys_spawn(const char* path) noexcept;

} // namespace kernel::process
