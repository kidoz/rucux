// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace kernel::gui {

long sys_display_ctl(uint32_t op, void* arg, size_t arg_size, uintptr_t flags) noexcept;
long sys_input_ctl(uint32_t op, void* arg, size_t arg_size, uintptr_t flags) noexcept;
long sys_gui_session(uint32_t op, void* arg, size_t arg_size, uintptr_t flags) noexcept;

} // namespace kernel::gui
