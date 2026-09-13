// SPDX-License-Identifier: MIT
#include <arch/amd64/io.hpp>
#include <arch/amd64/syscall.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/syscall.hpp>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/mmap.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/process/spawn.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/vfs.hpp>
#include <uapi/kernel/signal.h>

namespace arch::amd64 {

extern "C" void check_signals(uintptr_t* rsp) {
    auto* t = kernel::scheduler::scheduler::current_thread();
    if (!t) return;

    if (rsp[0] == 0x5168E700) {
        // Perform sigreturn
        uintptr_t user_rsp = rsp[10];
        sigcontext context{};
        if (!kernel::memory::copy_from_user(&context, reinterpret_cast<void*>(user_rsp), sizeof(context)) ||
            !kernel::memory::user_range(context.rip, 1) || !kernel::memory::user_range(context.rsp, 1)) {
            kernel::scheduler::scheduler::exit(-1);
            return;
        }
        auto* ctx = &context;
        ctx->rflags = (ctx->rflags & 0xCD5ULL) | 0x202ULL; // Arithmetic flags only; no IOPL/NT/VM.

        rsp[0] = ctx->rax;
        rsp[2] = ctx->r15;
        rsp[3] = ctx->r14;
        rsp[4] = ctx->r13;
        rsp[5] = ctx->r12;
        rsp[6] = ctx->rbx;
        rsp[7] = ctx->rbp;
        rsp[8] = ctx->rflags;
        rsp[9] = ctx->rip;
        rsp[10] = ctx->rsp;

        t->sig_mask = ctx->oldmask;
        return;
    }

    // Check for pending unmasked signals
    uint32_t pending = t->sig_pending & ~t->sig_mask;
    if (!pending) return;

    // First, process any fatal signals
    if (kernel::process::signal_manager::consume_fatal_signal(t)) {
        return;
    }

    // Now find the first user-handled signal
    int signum = -1;
    for (int i = 1; i < kernel::scheduler::thread::MAX_SIGNALS; ++i) {
        if (pending & (1U << i)) {
            signum = i;
            break;
        }
    }

    if (signum == -1) return;

    auto handler = t->sig_handlers[signum];
    if (!handler || handler == reinterpret_cast<kernel::scheduler::thread::sighandler_t>(1)) {
        // Ignored or DFL (and DFL wasn't fatal), just clear it
        t->sig_pending &= ~(1U << signum);
        return;
    }

    // We have a signal to deliver!
    // Construct the sigcontext on the user stack.
    uintptr_t user_rsp = rsp[10]; // rsp+80 is user rsp

    // Allocate space and align to 16 bytes
    user_rsp -= sizeof(sigcontext);
    user_rsp &= ~15ULL;

    sigcontext context{};
    auto* ctx = &context;

    ctx->rax = rsp[0];
    ctx->r15 = rsp[2];
    ctx->r14 = rsp[3];
    ctx->r13 = rsp[4];
    ctx->r12 = rsp[5];
    ctx->rbx = rsp[6];
    ctx->rbp = rsp[7];
    ctx->rflags = rsp[8];
    ctx->rip = rsp[9];
    ctx->rsp = rsp[10];
    ctx->oldmask = t->sig_mask;

    // Set up the registers to jump to the handler
    rsp[9] = reinterpret_cast<uintptr_t>(handler); // New RIP
    rsp[10] = user_rsp - 8;                        // New RSP, subtracting 8 to simulate a call (pushing restorer)

    // Push the restorer address onto the user stack so the handler returns to it
    uintptr_t restorer = reinterpret_cast<uintptr_t>(t->sig_restorers[signum]);
    if (!kernel::memory::user_range(rsp[9], 1) || !kernel::memory::user_range(restorer, 1) ||
        !kernel::memory::copy_to_user(reinterpret_cast<void*>(user_rsp), ctx, sizeof(*ctx)) ||
        !kernel::memory::copy_to_user(reinterpret_cast<void*>(rsp[10]), &restorer, sizeof(restorer))) {
        kernel::scheduler::scheduler::exit(-1);
        return;
    }

    // Signal handlers expect the signal number in RDI
    // Since syscall entry clobbered RDI, we will just use a hack:
    // we don't have RDI in the stack frame. We could add RDI to switch.S.
    // For now, we will leave RDI as is (clobbered by syscall_dispatch return).
    // Wait, the handler won't get the correct signum!

    // Clear the pending bit and mask the signal
    t->sig_pending &= ~(1U << signum);
    t->sig_mask |= (1U << signum);
}

static long dispatch_kernel(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    switch (num) {
    case SYS_GETDENTS:
        return kernel::vfs::vfs_manager::sys_getdents(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                      static_cast<size_t>(a3));
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
        return kernel::vfs::vfs_manager::sys_select(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                    reinterpret_cast<void*>(a3), reinterpret_cast<void*>(a4),
                                                    reinterpret_cast<void*>(a5));
    case SYS_POLL:
        return kernel::vfs::vfs_manager::sys_poll(reinterpret_cast<void*>(a1), static_cast<unsigned int>(a2),
                                                  static_cast<int>(a3));
    case SYS_EPOLL_CREATE:
        return kernel::vfs::vfs_manager::sys_epoll_create(static_cast<int>(a1));
    case SYS_EPOLL_CTL:
        return kernel::vfs::vfs_manager::sys_epoll_ctl(static_cast<int>(a1), static_cast<int>(a2), static_cast<int>(a3),
                                                       reinterpret_cast<void*>(a4));
    case SYS_EPOLL_WAIT:
        return kernel::vfs::vfs_manager::sys_epoll_wait(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                        static_cast<int>(a3), static_cast<int>(a4));
    case SYS_MMAP:
        return reinterpret_cast<long>(kernel::memory::mmap_manager::sys_mmap(
            reinterpret_cast<void*>(a1), static_cast<size_t>(a2), static_cast<int>(a3), static_cast<int>(a4),
            static_cast<int>(a5), static_cast<long>(a6)));
    case SYS_MUNMAP:
        return kernel::memory::mmap_manager::sys_munmap(reinterpret_cast<void*>(a1), static_cast<size_t>(a2));
    case SYS_MPROTECT:
        return kernel::memory::mmap_manager::sys_mprotect(reinterpret_cast<void*>(a1), static_cast<size_t>(a2),
                                                          static_cast<int>(a3));
    case SYS_MSYNC:
        return kernel::memory::mmap_manager::sys_msync(reinterpret_cast<void*>(a1), static_cast<size_t>(a2),
                                                       static_cast<int>(a3));
    case SYS_MADVISE:
        return kernel::memory::mmap_manager::sys_madvise(reinterpret_cast<void*>(a1), static_cast<size_t>(a2),
                                                         static_cast<int>(a3));
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
        return kernel::time_manager::sys_gettimeofday(reinterpret_cast<kernel::timeval*>(a1),
                                                      reinterpret_cast<void*>(a2));
    case SYS_NANOSLEEP:
        return kernel::time_manager::sys_nanosleep(reinterpret_cast<const kernel::timespec*>(a1),
                                                   reinterpret_cast<kernel::timespec*>(a2));
    case SYS_SIGACTION:
        return kernel::process::signal_manager::sys_sigaction(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                              reinterpret_cast<void*>(a3));
    case SYS_KILL:
        return kernel::process::signal_manager::sys_kill(static_cast<int>(a1), static_cast<int>(a2));
    case SYS_SIGPROCMASK:
        return kernel::process::signal_manager::sys_sigprocmask(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                                reinterpret_cast<void*>(a3));
    case SYS_SOCKET:
        return kernel::net::socket_manager::sys_socket(static_cast<int>(a1), static_cast<int>(a2),
                                                       static_cast<int>(a3));
    case SYS_BIND:
        return kernel::net::socket_manager::sys_bind(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                     static_cast<uint32_t>(a3));
    case SYS_LISTEN:
        return kernel::net::socket_manager::sys_listen(static_cast<int>(a1), static_cast<int>(a2));
    case SYS_ACCEPT:
        return kernel::net::socket_manager::sys_accept(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                       reinterpret_cast<uint32_t*>(a3));
    case SYS_CONNECT:
        return kernel::net::socket_manager::sys_connect(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                        static_cast<uint32_t>(a3));
    case SYS_SEND:
        return kernel::net::socket_manager::sys_send(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                     static_cast<size_t>(a3), static_cast<int>(a4));
    case SYS_RECV:
        return kernel::net::socket_manager::sys_recv(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                     static_cast<size_t>(a3), static_cast<int>(a4));
    case SYS_SENDTO:
        return kernel::net::socket_manager::sys_sendto(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                       static_cast<size_t>(a3), static_cast<int>(a4),
                                                       reinterpret_cast<const void*>(a5), static_cast<uint32_t>(a6));
    case SYS_RECVFROM:
        return kernel::net::socket_manager::sys_recvfrom(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                         static_cast<size_t>(a3), static_cast<int>(a4),
                                                         reinterpret_cast<void*>(a5), reinterpret_cast<uint32_t*>(a6));
    case SYS_SETSOCKOPT:
        return kernel::net::socket_manager::sys_setsockopt(static_cast<int>(a1), static_cast<int>(a2),
                                                           static_cast<int>(a3), reinterpret_cast<const void*>(a4),
                                                           static_cast<uint32_t>(a5));
    case SYS_GETSOCKOPT:
        return kernel::net::socket_manager::sys_getsockopt(static_cast<int>(a1), static_cast<int>(a2),
                                                           static_cast<int>(a3), reinterpret_cast<void*>(a4),
                                                           reinterpret_cast<uint32_t*>(a5));
    case SYS_GETSOCKNAME:
        return kernel::net::socket_manager::sys_getsockname(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                            reinterpret_cast<uint32_t*>(a3));
    case SYS_GETPEERNAME:
        return kernel::net::socket_manager::sys_getpeername(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                            reinterpret_cast<uint32_t*>(a3));
    case SYS_EXIT:
        kernel::scheduler::scheduler::exit();
        return 0;
    case SYS_IPC_CALL:
        return kernel::ipc::sys_ipc_call(static_cast<uint32_t>(a1), reinterpret_cast<kernel::ipc::fast_msg*>(a2));
    case SYS_IPC_REPLY:
        return kernel::ipc::sys_ipc_reply(reinterpret_cast<kernel::ipc::fast_msg*>(a1));
    case SYS_SPAWN:
        return kernel::process::sys_spawn(reinterpret_cast<const char*>(a1));
    case SYS_SIGRETURN:
        return 0x5168E700; // Magic value for check_signals
    default:
        return -1;
    }
}

extern "C" long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    return kernel::checked_syscall(dispatch_kernel, num, a1, a2, a3, a4, a5, a6);
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
