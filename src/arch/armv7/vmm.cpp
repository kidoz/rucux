// SPDX-License-Identifier: MIT
#include <kernel/memory/vmm.hpp>

namespace kernel::memory {

void vmm::init() noexcept {
    // Placeholder for ARMv7 translation tables
}

void vmm::map(uintptr_t virt, uintptr_t phys, page_flags flags) noexcept {
    (void)virt;
    (void)phys;
    (void)flags;
}

void vmm::unmap(uintptr_t virt) noexcept {
    (void)virt;
}

void vmm::switch_to(uintptr_t ttbr_phys) noexcept {
    (void)ttbr_phys;
}

uintptr_t vmm::get_active_page_table() noexcept {
    return 0; // Placeholder
}

void vmm::disable_write_protect() noexcept {
    // Placeholder
}

void vmm::enable_write_protect() noexcept {
    // Placeholder
}

} // namespace kernel::memory
