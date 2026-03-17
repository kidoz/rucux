// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/syscall.hpp>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/mmap.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>

namespace arch::amd64 {

extern "C" long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    switch (num) {
    case SYS_OPEN:
        return kernel::vfs::vfs_manager::sys_open(reinterpret_cast<const char*>(a1), static_cast<int>(a2));
    case SYS_READ:
        return kernel::vfs::vfs_manager::sys_read(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                  static_cast<size_t>(a3));
    case SYS_WRITE:
        return kernel::vfs::vfs_manager::sys_write(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                   static_cast<size_t>(a3));
    case SYS_CLOSE:
        return kernel::vfs::vfs_manager::sys_close(static_cast<int>(a1));
    case SYS_IOCTL:
        return kernel::vfs::vfs_manager::sys_ioctl(static_cast<int>(a1), static_cast<unsigned long>(a2),
                                                   reinterpret_cast<void*>(a3));
    case SYS_MMAP:
        return reinterpret_cast<long>(kernel::memory::mmap_manager::sys_mmap(
            reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3), static_cast<int>(a4),
            static_cast<int>(a5), static_cast<long>(a6)));
    case SYS_MUNMAP:
        return kernel::memory::mmap_manager::sys_munmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2));
    case SYS_CLONE:
        return kernel::scheduler::scheduler::sys_clone(reinterpret_cast<void*>(a1), reinterpret_cast<void*>(a2),
                                                       reinterpret_cast<void*>(a3));
    case SYS_FUTEX:
        return kernel::scheduler::scheduler::sys_futex(reinterpret_cast<uint32_t*>(a1), static_cast<int>(a2),
                                                       static_cast<uint32_t>(a3));
    case SYS_YIELD:
        kernel::scheduler::scheduler::yield();
        return 0;
    case SYS_IPC_SEND:
        kernel::ipc::ipc_manager::send_sync(static_cast<uint32_t>(a1),
                                            *reinterpret_cast<const kernel::ipc::message*>(a2));
        return 0;
    case SYS_IPC_RECV:
        kernel::ipc::ipc_manager::receive_sync(*reinterpret_cast<kernel::ipc::message*>(a1));
        return 0;
    case SYS_OUTB:
        outb(static_cast<uint16_t>(a1), static_cast<uint8_t>(a2));
        return 0;
    case SYS_INB:
        return inb(static_cast<uint16_t>(a1));
    case SYS_IRQ_WAIT:
        kernel::scheduler::scheduler::wait_for_irq(static_cast<uint8_t>(a1));
        return 0;
    case SYS_EXIT:
        kernel::scheduler::scheduler::exit();
        return 0; // We will not return to user space
    default:
        return -1;
    }
}

extern "C" void syscall_entry();

void syscall_init() noexcept {
    // EFER MSR: Enable syscall/sysret
    asm volatile("rdmsr\n"
                 "or $1, %%eax\n"
                 "wrmsr\n"
                 :
                 : "c"(0xC0000080)
                 : "rax", "rdx");

    // STAR MSR: Set kernel/user selectors
    uint64_t star = (0x08ULL << 32) | (0x10ULL << 48); // sysret uses base+16 for CS (0x20) and base+8 for SS (0x18)
    asm volatile("wrmsr\n"
                 :
                 : "c"(0xC0000081), "a"(static_cast<uint32_t>(star)), "d"(static_cast<uint32_t>(star >> 32)));

    // LSTAR MSR: Entry point
    uint64_t lstar = reinterpret_cast<uint64_t>(syscall_entry);
    asm volatile("wrmsr\n"
                 :
                 : "c"(0xC0000082), "a"(static_cast<uint32_t>(lstar)), "d"(static_cast<uint32_t>(lstar >> 32)));

    // SFMASK MSR: Flags to mask on syscall
    asm volatile("wrmsr\n" : : "c"(0xC0000084), "a"(0x200), "d"(0));
}

} // namespace arch::amd64
