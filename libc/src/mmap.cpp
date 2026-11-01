// SPDX-License-Identifier: MIT
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>

extern "C" void* mmap(void* address, size_t length, int prot, int flags, int fd, off_t offset) {
    static_assert(sizeof(off_t) <= sizeof(long));
    const long result = syscall(SYS_MMAP, reinterpret_cast<long>(address), static_cast<long>(length), prot, flags, fd,
                                static_cast<long>(offset));
    // ARMv7 user addresses can have their sign bit set. Only reserved syscall
    // error values, rather than all negative results, represent failures.
    if (static_cast<unsigned long>(result) >= static_cast<unsigned long>(-4095L)) {
        errno = static_cast<int>(-result);
        return MAP_FAILED;
    }
    return reinterpret_cast<void*>(result);
}
