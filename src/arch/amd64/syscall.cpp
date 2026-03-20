// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/syscall.hpp>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/mmap.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <kernel/time.hpp>

namespace arch::amd64 {

extern "C" long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
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
    case SYS_LSEEK:
        return kernel::vfs::vfs_manager::sys_lseek(static_cast<int>(a1), static_cast<long>(a2), static_cast<int>(a3));
    case SYS_STAT:
        return kernel::vfs::vfs_manager::sys_stat(reinterpret_cast<const char*>(a1), reinterpret_cast<void*>(a2));
    case SYS_FSTAT:
        return kernel::vfs::vfs_manager::sys_fstat(static_cast<int>(a1), reinterpret_cast<void*>(a2));
    case SYS_FTRUNCATE:
        return kernel::vfs::vfs_manager::sys_ftruncate(static_cast<int>(a1), static_cast<long>(a2));
    case SYS_FSYNC:
        return kernel::vfs::vfs_manager::sys_fsync(static_cast<int>(a1));
    case SYS_FCNTL:
        return kernel::vfs::vfs_manager::sys_fcntl(static_cast<int>(a1), static_cast<int>(a2), static_cast<long>(a3));
    case SYS_SELECT:
        return kernel::vfs::vfs_manager::sys_select(static_cast<int>(a1), reinterpret_cast<void*>(a2), reinterpret_cast<void*>(a3), reinterpret_cast<void*>(a4), reinterpret_cast<void*>(a5));
    case SYS_POLL:
        return kernel::vfs::vfs_manager::sys_poll(reinterpret_cast<void*>(a1), static_cast<unsigned int>(a2), static_cast<int>(a3));
    case SYS_EPOLL_CREATE:
        return kernel::vfs::vfs_manager::sys_epoll_create(static_cast<int>(a1));
    case SYS_EPOLL_CTL:
        return kernel::vfs::vfs_manager::sys_epoll_ctl(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<void*>(a4));
    case SYS_EPOLL_WAIT:
        return kernel::vfs::vfs_manager::sys_epoll_wait(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<int>(a3), static_cast<int>(a4));
    case SYS_MMAP:
        return reinterpret_cast<long>(kernel::memory::mmap_manager::sys_mmap(
            reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3), static_cast<int>(a4),
            static_cast<int>(a5), static_cast<long>(a6)));
    case SYS_MUNMAP:
        return kernel::memory::mmap_manager::sys_munmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2));
    case SYS_MPROTECT:
        return kernel::memory::mmap_manager::sys_mprotect(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));
    case SYS_MSYNC:
        return kernel::memory::mmap_manager::sys_msync(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));
    case SYS_MADVISE:
        return kernel::memory::mmap_manager::sys_madvise(reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3));
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
    case SYS_CLOCK_GETTIME:
        return kernel::time_manager::sys_clock_gettime(static_cast<int>(a1), reinterpret_cast<kernel::timespec*>(a2));
    case SYS_GETTIMEOFDAY:
        return kernel::time_manager::sys_gettimeofday(reinterpret_cast<kernel::timeval*>(a1), reinterpret_cast<void*>(a2));
    case SYS_NANOSLEEP:
        return kernel::time_manager::sys_nanosleep(reinterpret_cast<const kernel::timespec*>(a1), reinterpret_cast<kernel::timespec*>(a2));
    case SYS_SIGACTION:
        return kernel::process::signal_manager::sys_sigaction(static_cast<int>(a1), reinterpret_cast<const void*>(a2), reinterpret_cast<void*>(a3));
    case SYS_KILL:
        return kernel::process::signal_manager::sys_kill(static_cast<int>(a1), static_cast<int>(a2));
    case SYS_SIGPROCMASK:
        return kernel::process::signal_manager::sys_sigprocmask(static_cast<int>(a1), reinterpret_cast<const void*>(a2), reinterpret_cast<void*>(a3));
    case SYS_SOCKET:
        return kernel::net::socket_manager::sys_socket(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3));
    case SYS_BIND:
        return kernel::net::socket_manager::sys_bind(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<uint32_t>(a3));
    case SYS_LISTEN:
        return kernel::net::socket_manager::sys_listen(static_cast<int>(a1), static_cast<int>(a2));
    case SYS_ACCEPT:
        return kernel::net::socket_manager::sys_accept(static_cast<int>(a1), reinterpret_cast<void*>(a2), reinterpret_cast<uint32_t*>(a3));
    case SYS_CONNECT:
        return kernel::net::socket_manager::sys_connect(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<uint32_t>(a3));
    case SYS_SEND:
        return kernel::net::socket_manager::sys_send(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4));
    case SYS_RECV:
        return kernel::net::socket_manager::sys_recv(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4));
    case SYS_SENDTO:
        return kernel::net::socket_manager::sys_sendto(static_cast<int>(a1), reinterpret_cast<const void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4), reinterpret_cast<const void*>(a5), static_cast<uint32_t>(a6));
    case SYS_RECVFROM:
        return kernel::net::socket_manager::sys_recvfrom(static_cast<int>(a1), reinterpret_cast<void*>(a2), static_cast<size_t>(a3), static_cast<int>(a4), reinterpret_cast<void*>(a5), reinterpret_cast<uint32_t*>(a6));
    case SYS_SETSOCKOPT:
        return kernel::net::socket_manager::sys_setsockopt(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<const void*>(a4), static_cast<uint32_t>(a5));
    case SYS_GETSOCKOPT:
        return kernel::net::socket_manager::sys_getsockopt(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3), reinterpret_cast<void*>(a4), reinterpret_cast<uint32_t*>(a5));
    case SYS_GETSOCKNAME:
        return kernel::net::socket_manager::sys_getsockname(static_cast<int>(a1), reinterpret_cast<void*>(a2), reinterpret_cast<uint32_t*>(a3));
    case SYS_GETPEERNAME:
        return kernel::net::socket_manager::sys_getpeername(static_cast<int>(a1), reinterpret_cast<void*>(a2), reinterpret_cast<uint32_t*>(a3));
    case SYS_EXIT:
        kernel::scheduler::scheduler::exit();
        return 0;
    case SYS_IPC_CALL:
        return kernel::ipc::sys_ipc_call(static_cast<uint32_t>(a1),
                                         reinterpret_cast<kernel::ipc::fast_msg*>(a2));
    case SYS_IPC_REPLY:
        return kernel::ipc::sys_ipc_reply(reinterpret_cast<kernel::ipc::fast_msg*>(a1));
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
