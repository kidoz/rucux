// SPDX-License-Identifier: MIT
#include <arch/armv7/syscall.hpp>

namespace arch::armv7 {

void syscall_init() noexcept {
    // For ARMv7, we set up the SVC (Supervisor Call) handler in the vector table.
    // This was already done in exception_handler.
}

} // namespace arch::armv7
