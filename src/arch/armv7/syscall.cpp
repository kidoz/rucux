// SPDX-License-Identifier: MIT
#include <arch/armv7/syscall.hpp>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/mmap.hpp>
#include <kernel/process/signal.hpp>
#include <uapi/kernel/signal.h>

namespace arch::armv7 {

static inline uint32_t get_user_sp() noexcept {
    uint32_t sp_usr;
    asm volatile(
        "mrs r1, cpsr\n"
        "bic r2, r1, #0x1f\n"
        "orr r2, r2, #0x1f\n"
        "msr cpsr_c, r2\n"
        "mov %0, sp\n"
        "msr cpsr_c, r1\n"
        : "=r"(sp_usr) : : "r1", "r2", "memory"
    );
    return sp_usr;
}

static inline void set_user_sp(uint32_t sp_usr) noexcept {
    asm volatile(
        "mrs r1, cpsr\n"
        "bic r2, r1, #0x1f\n"
        "orr r2, r2, #0x1f\n"
        "msr cpsr_c, r2\n"
        "mov sp, %0\n"
        "msr cpsr_c, r1\n"
        : : "r"(sp_usr) : "r1", "r2", "memory"
    );
}

static inline uint32_t get_user_lr() noexcept {
    uint32_t lr_usr;
    asm volatile(
        "mrs r1, cpsr\n"
        "bic r2, r1, #0x1f\n"
        "orr r2, r2, #0x1f\n"
        "msr cpsr_c, r2\n"
        "mov %0, lr\n"
        "msr cpsr_c, r1\n"
        : "=r"(lr_usr) : : "r1", "r2", "memory"
    );
    return lr_usr;
}

static inline void set_user_lr(uint32_t lr_usr) noexcept {
    asm volatile(
        "mrs r1, cpsr\n"
        "bic r2, r1, #0x1f\n"
        "orr r2, r2, #0x1f\n"
        "msr cpsr_c, r2\n"
        "mov lr, %0\n"
        "msr cpsr_c, r1\n"
        : : "r"(lr_usr) : "r1", "r2", "memory"
    );
}

static inline uint32_t get_spsr() noexcept {
    uint32_t val;
    asm volatile("mrs %0, spsr" : "=r"(val));
    return val;
}

static inline void set_spsr(uint32_t val) noexcept {
    asm volatile("msr spsr_cxsf, %0" : : "r"(val));
}

extern "C" void check_signals(uintptr_t* sp_ptr) {
    auto* t = kernel::scheduler::scheduler::current_thread();
    if (!t) return;

    if (sp_ptr[0] == 0x5168E700) {
        uint32_t user_sp = get_user_sp();
        auto* ctx = reinterpret_cast<sigcontext*>(user_sp);
        
        sp_ptr[0] = ctx->r0;
        sp_ptr[1] = ctx->r1;
        sp_ptr[2] = ctx->r2;
        sp_ptr[3] = ctx->r3;
        sp_ptr[4] = ctx->r4;
        sp_ptr[5] = ctx->r5;
        sp_ptr[6] = ctx->r6;
        sp_ptr[7] = ctx->r7;
        sp_ptr[8] = ctx->r8;
        sp_ptr[9] = ctx->r9;
        sp_ptr[10] = ctx->r10;
        sp_ptr[11] = ctx->r11;
        sp_ptr[12] = ctx->r12;
        sp_ptr[13] = ctx->pc;
        
        set_user_sp(ctx->sp);
        set_user_lr(ctx->lr);
        set_spsr(ctx->cpsr);
        
        t->sig_mask = ctx->oldmask;
        return;
    }

    uint32_t pending = t->sig_pending & ~t->sig_mask;
    if (!pending) return;

    if (kernel::process::signal_manager::consume_fatal_signal(t)) return;

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
        t->sig_pending &= ~(1U << signum);
        return;
    }

    uint32_t user_sp = get_user_sp();
    user_sp -= sizeof(sigcontext);
    user_sp &= ~7U; // 8-byte alignment

    auto* ctx = reinterpret_cast<sigcontext*>(user_sp);
    ctx->r0 = sp_ptr[0];
    ctx->r1 = sp_ptr[1];
    ctx->r2 = sp_ptr[2];
    ctx->r3 = sp_ptr[3];
    ctx->r4 = sp_ptr[4];
    ctx->r5 = sp_ptr[5];
    ctx->r6 = sp_ptr[6];
    ctx->r7 = sp_ptr[7];
    ctx->r8 = sp_ptr[8];
    ctx->r9 = sp_ptr[9];
    ctx->r10 = sp_ptr[10];
    ctx->r11 = sp_ptr[11];
    ctx->r12 = sp_ptr[12];
    ctx->sp = get_user_sp();
    ctx->lr = get_user_lr();
    ctx->pc = sp_ptr[13]; // return address
    ctx->cpsr = get_spsr();
    ctx->oldmask = t->sig_mask;

    // Set registers to jump to handler
    sp_ptr[13] = reinterpret_cast<uint32_t>(handler); // pc
    set_user_sp(user_sp);
    set_user_lr(reinterpret_cast<uint32_t>(t->sig_restorers[signum])); // lr = trampoline

    t->sig_pending &= ~(1U << signum);
    t->sig_mask |= (1U << signum);
}

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
    case SYS_SPAWN:
        return kernel::process::sys_spawn(reinterpret_cast<const char*>(a1));
    default:
        return -1;
    }
}

void syscall_init() noexcept {
    // SVC handler is set up via the vector table in exception_vectors.S.
    // No additional MSR setup needed on ARM (unlike x86 SYSCALL/SYSRET).
}

} // namespace arch::armv7
