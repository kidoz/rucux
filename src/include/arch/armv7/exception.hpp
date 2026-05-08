// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

namespace arch::armv7 {

void exceptions_init() noexcept;
void save_user_return_context(uintptr_t* user_sp, uintptr_t* user_lr, uint32_t* user_spsr) noexcept;
void restore_user_return_context(uintptr_t user_sp, uintptr_t user_lr, uint32_t user_spsr) noexcept;

} // namespace arch::armv7
