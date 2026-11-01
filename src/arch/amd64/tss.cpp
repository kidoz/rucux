// SPDX-License-Identifier: MIT
#include <arch/amd64/tss.hpp>
#include <kernel/cpu/percpu.hpp>
#include <lib/string.hpp>

namespace arch::amd64 {

static tss_entry g_tss[kernel::cpu::MAX_CPUS];
alignas(16) static uint8_t nmi_stacks[kernel::cpu::MAX_CPUS][8192];

void tss_init(uint32_t cpu_id) noexcept {
    auto& tss = g_tss[cpu_id];
    lib::memset(&tss, 0, sizeof(tss));
    tss.iopb_offset = sizeof(tss);
    tss.ist[0] = reinterpret_cast<uintptr_t>(nmi_stacks[cpu_id] + sizeof(nmi_stacks[cpu_id]));
}

void tss_set_rsp0(uintptr_t rsp0) noexcept {
    g_tss[kernel::cpu::this_cpu()->cpu_id].rsp[0] = rsp0;
}

tss_entry* get_tss(uint32_t cpu_id) noexcept {
    return &g_tss[cpu_id];
}

} // namespace arch::amd64
