// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

void tss_init() noexcept;
void tss_set_rsp0(uintptr_t rsp0) noexcept;

} // namespace arch::amd64
