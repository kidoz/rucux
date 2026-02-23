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

} // namespace kernel::memory
