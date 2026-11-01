// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <arch/amd64/tss.hpp>
#include <cstring>
#include <kernel/boot_protocol.hpp>
#include <kernel/cpu/percpu.hpp>
namespace kernel::cpu {
per_cpu g_percpu[MAX_CPUS]{};
unsigned selected;
per_cpu* this_cpu() noexcept {
    return &g_percpu[selected];
}
} // namespace kernel::cpu
namespace lib {
void* memset(void* p, int c, size_t n) {
    return std::memset(p, c, n);
}
} // namespace lib
TEST(tss_and_nmi_stacks_are_private_per_cpu) {
    using namespace arch::amd64;
    for (unsigned id = 0; id < kernel::cpu::MAX_CPUS; ++id) {
        tss_init(id);
        const auto* tss = get_tss(id);
        ASSERT_EQ(tss->iopb_offset, sizeof(tss_entry));
        ASSERT_EQ(tss->rsp[0], 0U);
        ASSERT(tss->ist[0] != 0);
        ASSERT_EQ(tss->ist[0] % 16, 0U);
        for (unsigned other = 0; other < id; ++other) {
            ASSERT(tss != get_tss(other));
            ASSERT(tss->ist[0] != get_tss(other)->ist[0]);
        }
    }
}
TEST(updating_ring_zero_stack_preserves_other_cpus_and_nmi_stack) {
    using namespace arch::amd64;
    for (unsigned id = 0; id < 4; ++id) {
        kernel::cpu::selected = id;
        kernel::cpu::g_percpu[id].cpu_id = id;
        tss_init(id);
        tss_set_rsp0(0x10000 + id * 0x1000);
    }
    const auto nmi = get_tss(2)->ist[0];
    kernel::cpu::selected = 2;
    tss_set_rsp0(0xF0000);
    for (unsigned id = 0; id < 4; ++id)
        ASSERT_EQ(get_tss(id)->rsp[0], id == 2 ? 0xF0000U : 0x10000U + id * 0x1000U);
    ASSERT_EQ(get_tss(2)->ist[0], nmi);
}
TEST(boot_protocol_v2_appends_the_reserved_page_without_changing_the_legacy_prefix) {
    ASSERT(RUCUX_BOOT_MAGIC != RUCUX_BOOT_MAGIC_V1);
    ASSERT_EQ(__builtin_offsetof(rucux_boot_info, ap_trampoline),
              __builtin_offsetof(rucux_boot_info, dtb_address) + sizeof(uint64_t));
    if constexpr (sizeof(void*) == 8) {
        ASSERT_EQ(__builtin_offsetof(rucux_boot_info, acpi_rsdp), 56U);
        ASSERT_EQ(__builtin_offsetof(rucux_boot_info, ap_trampoline), 80U);
        ASSERT_EQ(sizeof(rucux_boot_info), 88U);
    }
}
int main() {
    return RUN_ALL_TESTS("amd64 per-CPU TSS and NMI stacks");
}
