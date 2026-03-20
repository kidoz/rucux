// SPDX-License-Identifier: MIT
#include <arch/armv7/syscall.hpp>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/mmap.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>

namespace arch::armv7 {

// Shared syscall dispatcher — called from the SVC assembly stub.
// Same interface as amd64: num in first arg, up to 6 args following.
extern "C" long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    switch (num) {
    case SYS_EXIT:
        kernel::scheduler::scheduler::exit();
        return 0;
    case SYS_WRITE:
        return kernel::vfs::vfs_manager::sys_write(static_cast<int>(a1),
                    reinterpret_cast<const void*>(a2), static_cast<size_t>(a3));
    case SYS_READ:
        return kernel::vfs::vfs_manager::sys_read(static_cast<int>(a1),
                    reinterpret_cast<void*>(a2), static_cast<size_t>(a3));
    case SYS_OPEN:
        return kernel::vfs::vfs_manager::sys_open(reinterpret_cast<const char*>(a1),
                    static_cast<int>(a2));
    case SYS_CLOSE:
        return kernel::vfs::vfs_manager::sys_close(static_cast<int>(a1));
    case SYS_IOCTL:
        return kernel::vfs::vfs_manager::sys_ioctl(static_cast<int>(a1),
                    static_cast<unsigned long>(a2), reinterpret_cast<void*>(a3));
    case SYS_LSEEK:
        return kernel::vfs::vfs_manager::sys_lseek(static_cast<int>(a1),
                    static_cast<long>(a2), static_cast<int>(a3));
    case SYS_STAT:
        return kernel::vfs::vfs_manager::sys_stat(reinterpret_cast<const char*>(a1),
                    reinterpret_cast<void*>(a2));
    case SYS_FSTAT:
        return kernel::vfs::vfs_manager::sys_fstat(static_cast<int>(a1),
                    reinterpret_cast<void*>(a2));
    case SYS_MMAP:
        return reinterpret_cast<long>(kernel::memory::mmap_manager::sys_mmap(
                    reinterpret_cast<void*>(a1), static_cast<size_t>(a2),
                    static_cast<int>(a3), static_cast<int>(a4),
                    static_cast<int>(a5), static_cast<long>(a6)));
    case SYS_MUNMAP:
        return kernel::memory::mmap_manager::sys_munmap(
                    reinterpret_cast<void*>(a1), static_cast<size_t>(a2));
    case SYS_CLONE:
        return kernel::scheduler::scheduler::sys_clone(
                    reinterpret_cast<void*>(a1), reinterpret_cast<void*>(a2),
                    reinterpret_cast<void*>(a3));
    case SYS_FUTEX:
        return kernel::scheduler::scheduler::sys_futex(
                    reinterpret_cast<uint32_t*>(a1), static_cast<int>(a2),
                    static_cast<uint32_t>(a3));
    case SYS_YIELD:
        kernel::scheduler::scheduler::yield();
        return 0;
    case SYS_IPC_SEND:
        kernel::ipc::ipc_manager::send_sync(static_cast<uint32_t>(a1),
                    *reinterpret_cast<const kernel::ipc::message*>(a2));
        return 0;
    case SYS_IPC_RECV:
        kernel::ipc::ipc_manager::receive_sync(
                    *reinterpret_cast<kernel::ipc::message*>(a1));
        return 0;
    case SYS_IPC_CALL:
        return kernel::ipc::sys_ipc_call(static_cast<uint32_t>(a1),
                    reinterpret_cast<kernel::ipc::fast_msg*>(a2));
    case SYS_IPC_REPLY:
        return kernel::ipc::sys_ipc_reply(
                    reinterpret_cast<kernel::ipc::fast_msg*>(a1));
    default:
        return -1;
    }
}

void syscall_init() noexcept {
    // SVC handler is set up via the vector table in exception_vectors.S.
    // No additional MSR setup needed on ARM (unlike x86 SYSCALL/SYSRET).
}

} // namespace arch::armv7
