// SPDX-License-Identifier: MIT
//
// AArch64 synchronous-exception entry from EL0: syscalls and userspace faults.
//
// Calling convention follows Linux/AArch64: the syscall number is in x8 and
// arguments in x0-x5. The return value replaces the saved x0, so userspace
// sees it after ERET.

#include <arch/aarch64/syscall.hpp>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/print.hpp>
#include <kernel/process/spawn.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

namespace arch::aarch64 {

namespace {

// ESR_EL1.EC values we care about for exceptions taken from a lower EL.
constexpr uint64_t EC_SVC64 = 0x15;
constexpr uint64_t EC_INSTRUCTION_ABORT = 0x20;
constexpr uint64_t EC_DATA_ABORT = 0x24;

const char* ec_name(uint64_t ec) noexcept {
    switch (ec) {
    case EC_INSTRUCTION_ABORT:
        return "instruction abort";
    case EC_DATA_ABORT:
        return "data abort";
    default:
        return "unhandled synchronous exception";
    }
}

} // namespace

long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) noexcept {
    switch (num) {
    case SYS_EXIT:
        kernel::scheduler::scheduler::exit();
        return 0;
    case SYS_WRITE:
        return kernel::vfs::vfs_manager::sys_write(static_cast<int>(a1), reinterpret_cast<const void*>(a2),
                                                   static_cast<size_t>(a3));
    case SYS_READ:
        return kernel::vfs::vfs_manager::sys_read(static_cast<int>(a1), reinterpret_cast<void*>(a2),
                                                  static_cast<size_t>(a3));
    case SYS_OPEN:
        return kernel::vfs::vfs_manager::sys_open(reinterpret_cast<const char*>(a1), static_cast<int>(a2));
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
    case SYS_IPC_CALL:
        return kernel::ipc::sys_ipc_call(static_cast<uint32_t>(a1), reinterpret_cast<kernel::ipc::fast_msg*>(a2));
    case SYS_IPC_REPLY:
        return kernel::ipc::sys_ipc_reply(reinterpret_cast<kernel::ipc::fast_msg*>(a1));
    case SYS_SPAWN:
        return kernel::process::sys_spawn(reinterpret_cast<const char*>(a1));
    default:
        return -1;
    }
}

void syscall_init() noexcept {
    // SVC lands in the vector table set by exceptions_init(); AArch64 needs no
    // separate MSR programming the way x86 SYSCALL/SYSRET does.
}

} // namespace arch::aarch64

// frame[i] holds x<i> as saved by SAVE_REGS in exception_vectors.S.
extern "C" void aarch64_lower_sync_handler(uint64_t* frame) {
    uint64_t esr = 0;
    uint64_t elr = 0;
    uint64_t far = 0;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, elr_el1" : "=r"(elr));
    asm volatile("mrs %0, far_el1" : "=r"(far));

    const uint64_t ec = (esr >> 26) & 0x3F;

    if (ec == arch::aarch64::EC_SVC64) {
        frame[0] = static_cast<uint64_t>(arch::aarch64::syscall_dispatch(
            static_cast<long>(frame[8]), static_cast<long>(frame[0]), static_cast<long>(frame[1]),
            static_cast<long>(frame[2]), static_cast<long>(frame[3]), static_cast<long>(frame[4]),
            static_cast<long>(frame[5])));
        return;
    }

    // A fault in userspace may be a demand-paging miss. If the VMA layer backs
    // it, ERET simply retries the faulting instruction.
    if (ec == arch::aarch64::EC_DATA_ABORT || ec == arch::aarch64::EC_INSTRUCTION_ABORT) {
        if (kernel::memory::vmm::handle_page_fault(static_cast<uintptr_t>(far), esr))
            return;
    }

    kernel::print("\n*** EL0 {} ***\n", arch::aarch64::ec_name(ec));
    kernel::print("ESR_EL1 = {}, FAR_EL1 = {}, ELR_EL1 = {}\n",
                  reinterpret_cast<void*>(static_cast<uintptr_t>(esr)),
                  reinterpret_cast<void*>(static_cast<uintptr_t>(far)),
                  reinterpret_cast<void*>(static_cast<uintptr_t>(elr)));
    kernel::print("Terminating thread.\n");
    kernel::scheduler::scheduler::exit(-1);
}
