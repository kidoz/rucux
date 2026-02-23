// SPDX-License-Identifier: MIT
#include <arch/amd64/tss.hpp>
#include <lib/string.hpp>

namespace arch::amd64 {

static tss_entry g_tss;

void tss_init() noexcept {
    lib::memset(&g_tss, 0, sizeof(g_tss));
    g_tss.iopb_offset = sizeof(g_tss);
}

void tss_set_rsp0(uintptr_t rsp0) noexcept {
    g_tss.rsp[0] = rsp0;
}

tss_entry* get_tss() noexcept {
    return &g_tss;
}

} // namespace arch::amd64
