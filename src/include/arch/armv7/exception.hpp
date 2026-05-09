// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace arch::armv7 {

void init_exception_mode_stacks(uint32_t cpu_id) noexcept;
void exceptions_init() noexcept;
void save_user_return_context(uintptr_t* user_sp, uintptr_t* user_lr, uint32_t* user_spsr) noexcept;
void restore_user_return_context(uintptr_t user_sp, uintptr_t user_lr, uint32_t user_spsr) noexcept;

} // namespace arch::armv7
