// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

class mmap_manager {
public:
    static void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, long offset) noexcept;
    static int sys_munmap(void* addr, size_t length) noexcept;
    static int sys_mprotect(void* addr, size_t len, int prot) noexcept;
    static int sys_msync(void* addr, size_t length, int flags) noexcept;
    static int sys_madvise(void* addr, size_t length, int advice) noexcept;
};

} // namespace kernel::memory
