// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/tlb.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vmm.hpp>

namespace kernel::memory::tlb {
static_assert(cpu::MAX_CPUS == CPU_LIMIT);
root_set active;
uint32_t current_cpu() noexcept {
    // amd64 builds the boot root before installing GS; no AP can exist yet.
    return cpu::g_cpu_count.load(acquire) ? cpu::this_cpu()->cpu_id : 0;
}
} // namespace kernel::memory::tlb

namespace kernel::memory {
bool vmm::is_active(uintptr_t root) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    return tlb::active.mask(root) != 0;
}
} // namespace kernel::memory
