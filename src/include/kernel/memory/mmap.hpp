// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/memory/vmm.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

inline page_flags flags_for_prot(uint32_t prot) noexcept {
    constexpr uint32_t k_write = 0x02;
    constexpr uint32_t k_exec = 0x04;
    page_flags flags = page_flags::PRESENT;
    if (prot != 0) flags = flags | page_flags::USER;
    if ((prot & k_write) != 0) flags = flags | page_flags::WRITABLE;
    if ((prot & k_exec) == 0) flags = flags | page_flags::NO_EXECUTE;
    return flags;
}

class mmap_manager {
public:
    // Syscall result: user address or an encoded negative errno pointer. Libc
    // converts reserved error values to MAP_FAILED/errno on every architecture.
    static void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) noexcept;
    static int sys_munmap(void* addr, size_t length) noexcept;
    static int sys_mprotect(void* addr, size_t len, int prot) noexcept;
    static int sys_msync(void* addr, size_t length, int flags) noexcept;
    static int sys_madvise(void* addr, size_t length, int advice) noexcept;

    // Fill a not-present anonymous user page from the current address space.
    // File-backed pages are populated eagerly and never replaced with zeros.
    static bool handle_fault(uintptr_t fault_addr, bool write = false, bool execute = false) noexcept;
};

} // namespace kernel::memory
