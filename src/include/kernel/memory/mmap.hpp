// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/memory/vmm.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

inline page_flags flags_for_prot(uint32_t prot) noexcept {
    constexpr uint32_t k_write = 0x02;
    constexpr uint32_t k_exec = 0x04;
    page_flags flags = page_flags::PRESENT | page_flags::USER;
    if ((prot & k_write) != 0) flags = flags | page_flags::WRITABLE;
    if ((prot & k_exec) == 0) flags = flags | page_flags::NO_EXECUTE;
    return flags;
}

class mmap_manager {
public:
    static void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) noexcept;
    static int sys_munmap(void* addr, size_t length) noexcept;
    static int sys_mprotect(void* addr, size_t len, int prot) noexcept;
    static int sys_msync(void* addr, size_t length, int flags) noexcept;
    static int sys_madvise(void* addr, size_t length, int advice) noexcept;

    // Fill a not-present user page from the current address space. A miss,
    // a shared file frame, or an already-present page is not filled.
    static bool handle_fault(uintptr_t fault_addr) noexcept;
};

} // namespace kernel::memory
