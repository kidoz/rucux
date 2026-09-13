// SPDX-License-Identifier: MIT
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/syscall.hpp>
#include <kernel/time.hpp>
#include <knew.hpp>
#include <lib/string.hpp>
#include <uapi/kernel/stat.h>
#include <uapi/kernel/syscalls.h>

namespace kernel {
namespace {
constexpr size_t MAX_TRANSFER = 1024 * 1024;
constexpr size_t PATH_LIMIT = 1024;
constexpr long EFAULT_RESULT = -14;
constexpr long EINVAL_RESULT = -22;

enum direction { INPUT = 1, OUTPUT = 2, INOUT = 3 };
struct buffer {
    void* user = nullptr;
    uint8_t* data = nullptr;
    size_t size = 0;
    size_t output_size = 0;
    ~buffer() { delete[] data; }
};

struct arguments {
    long value[6];
    buffer buffers[6];
    long error = 0;

    bool marshal(unsigned index, size_t size, direction mode, bool optional = false, size_t capacity = 0) noexcept {
        if (optional && !value[index]) return true;
        if (size > MAX_TRANSFER || capacity > MAX_TRANSFER) {
            error = EINVAL_RESULT;
            return false;
        }
        auto& b = buffers[index];
        b.user = reinterpret_cast<void*>(value[index]);
        b.size = size;
        b.output_size = (mode & OUTPUT) ? size : 0;
        if (!memory::user_accessible(b.user, size, mode & OUTPUT)) {
            error = EFAULT_RESULT;
            return false;
        }
        const size_t allocated = capacity > size ? capacity : size;
        b.data = new uint8_t[allocated ? allocated : 1];
        if (!b.data) {
            error = -12;
            return false;
        }
        lib::memset(b.data, 0, allocated ? allocated : 1);
        if ((mode & INPUT) && !memory::copy_from_user(b.data, b.user, size)) {
            error = EFAULT_RESULT;
            return false;
        }
        value[index] = reinterpret_cast<long>(b.data);
        return true;
    }

    bool array(unsigned index, long count, size_t element, direction mode) noexcept {
        if (count < 0 || static_cast<size_t>(count) > MAX_TRANSFER / element) {
            error = EINVAL_RESULT;
            return false;
        }
        return marshal(index, static_cast<size_t>(count) * element, mode);
    }

    // Snapshot the caller's length once. Backends always receive storage large
    // enough for sockaddr_in, while copyout respects the caller's capacity.
    bool socket_address(unsigned addr, unsigned len, bool optional) noexcept {
        if (optional && !value[addr]) {
            value[len] = 0;
            return true;
        }
        if (!marshal(len, sizeof(uint32_t), INOUT)) return false;
        uint32_t capacity = *reinterpret_cast<uint32_t*>(buffers[len].data);
        size_t copied = capacity < 16 ? capacity : 16;
        return marshal(addr, copied, OUTPUT, false, 16);
    }
};
} // namespace

long checked_syscall(syscall_handler handler, long number, long a1, long a2, long a3, long a4, long a5,
                     long a6) noexcept {
    arguments a{{a1, a2, a3, a4, a5, a6}, {}, 0};
    bool ok = true;
    char path[PATH_LIMIT];
    switch (number) {
    case SYS_OPEN:
    case SYS_STAT:
    case SYS_SPAWN: {
        long result = memory::copy_user_string(path, reinterpret_cast<const char*>(a1), sizeof(path));
        if (result < 0) return result;
        a.value[0] = reinterpret_cast<long>(path);
        if (number == SYS_STAT) ok = a.marshal(1, sizeof(struct stat), OUTPUT);
        break;
    }
    case SYS_FSTAT:
        ok = a.marshal(1, sizeof(struct stat), OUTPUT);
        break;
    case SYS_READ:
    case SYS_RECV:
    case SYS_GETDENTS:
        ok = a.array(1, a3, 1, OUTPUT);
        break;
    case SYS_WRITE:
    case SYS_SEND:
        ok = a.array(1, a3, 1, INPUT);
        break;
    case SYS_IPC_SEND:
        ok = a.marshal(1, sizeof(ipc::message), INPUT);
        break;
    case SYS_IPC_RECV:
        ok = a.marshal(0, sizeof(ipc::message), OUTPUT);
        break;
    case SYS_IPC_CALL:
        ok = a.marshal(1, sizeof(ipc::fast_msg), INOUT);
        break;
    case SYS_IPC_REPLY:
        ok = a.marshal(0, sizeof(ipc::fast_msg), INPUT);
        break;
    case SYS_IOCTL:
        switch (a2) {
        case 0x5401:
            ok = a.marshal(2, 60, OUTPUT);
            break; // TCGETS, struct termios
        case 0x5402:
            ok = a.marshal(2, 60, INPUT);
            break; // TCSETS
        case 0x5412:
            ok = a.marshal(2, 1, INPUT);
            break; // TIOCSTI
        case 0x5413:
            ok = a.marshal(2, 8, OUTPUT);
            break; // TIOCGWINSZ
        default:
            return -25; // No unchecked, driver-specific pointer fallback.
        }
        break;
    case SYS_CLOCK_GETTIME:
        ok = a.marshal(1, sizeof(timespec), OUTPUT);
        break;
    case SYS_GETTIMEOFDAY:
        ok = a.marshal(0, sizeof(timeval), OUTPUT);
        a.value[1] = 0; // timezone is obsolete and ignored by the backend.
        break;
    case SYS_NANOSLEEP:
        ok = a.marshal(0, sizeof(timespec), INPUT) && a.marshal(1, sizeof(timespec), OUTPUT, true);
        break;
    case SYS_SIGACTION:
        ok = a.marshal(1, 2 * sizeof(uintptr_t) + 8, INPUT, true) &&
             a.marshal(2, 2 * sizeof(uintptr_t) + 8, OUTPUT, true);
        break;
    case SYS_SIGPROCMASK:
        ok = a.marshal(1, 4, INPUT, true) && a.marshal(2, 4, OUTPUT, true);
        break;
    case SYS_SELECT:
        if (a1 < 0 || a1 > 1024) return EINVAL_RESULT;
        ok = a.marshal(1, 128, INOUT, true) && a.marshal(2, 128, INOUT, true) && a.marshal(3, 128, INOUT, true) &&
             a.marshal(4, sizeof(timeval), INPUT, true);
        break;
    case SYS_POLL:
        ok = a.array(0, a2, 8, INOUT);
        break;
    case SYS_EPOLL_CTL:
        if (a2 != 2)
            ok = a.marshal(3, 12, INPUT);
        else
            a.value[3] = 0;
        break;
    case SYS_EPOLL_WAIT:
        if (a3 <= 0) return EINVAL_RESULT;
        ok = a.array(1, a3, 12, OUTPUT);
        break;
    case SYS_BIND:
    case SYS_CONNECT:
        if (a3 < 16) return EINVAL_RESULT;
        ok = a.marshal(1, 16, INPUT);
        a.value[2] = 16;
        break;
    case SYS_ACCEPT:
        ok = a.socket_address(1, 2, true);
        break;
    case SYS_GETSOCKNAME:
    case SYS_GETPEERNAME:
        ok = a.socket_address(1, 2, false);
        break;
    case SYS_SENDTO:
        if (a6 < 16) return EINVAL_RESULT;
        ok = a.array(1, a3, 1, INPUT) && a.marshal(4, 16, INPUT);
        a.value[5] = 16;
        break;
    case SYS_RECVFROM:
        ok = a.array(1, a3, 1, OUTPUT) && a.socket_address(4, 5, true);
        break;
    case SYS_SETSOCKOPT:
        ok = a.array(3, a5, 1, INPUT);
        break;
    case SYS_GETSOCKOPT:
        ok = a.marshal(4, 4, INOUT);
        if (ok) ok = a.marshal(3, *reinterpret_cast<uint32_t*>(a.buffers[4].data), OUTPUT);
        break;
    case SYS_CLONE:
        if (!memory::user_accessible(reinterpret_cast<void*>(a1), 1, false) || a2 < 16 ||
            !memory::user_accessible(reinterpret_cast<void*>(a2 - 16), 16, true))
            return EFAULT_RESULT;
        break; // Entry/stack/argument are user values, not kernel-dereferenced buffers.
    case SYS_FUTEX:
        if ((a1 & 3) || !memory::user_accessible(reinterpret_cast<void*>(a1), 4, false)) return EFAULT_RESULT;
        break; // futex_wait copies the word while holding its wait-bucket lock.
    case SYS_EXIT:
    case SYS_CLOSE:
    case SYS_OUTB:
    case SYS_INB:
    case SYS_IRQ_WAIT:
    case SYS_MMAP:
    case SYS_MUNMAP:
    case SYS_SOCKET:
    case SYS_LISTEN:
    case SYS_YIELD:
    case SYS_EPOLL_CREATE:
    case SYS_MPROTECT:
    case SYS_MSYNC:
    case SYS_MADVISE:
    case SYS_LSEEK:
    case SYS_FTRUNCATE:
    case SYS_FSYNC:
    case SYS_FCNTL:
    case SYS_KILL:
    case SYS_SIGRETURN:
        break;
    default:
        return -38;
    }
    if (!ok) return a.error;
    long result = handler(number, a.value[0], a.value[1], a.value[2], a.value[3], a.value[4], a.value[5]);
    if (result < 0) return result;
    if (number == SYS_READ || number == SYS_RECV || number == SYS_RECVFROM || number == SYS_GETDENTS ||
        number == SYS_EPOLL_WAIT) {
        size_t count = static_cast<size_t>(result);
        if (number == SYS_EPOLL_WAIT) count *= 12;
        if (count > a.buffers[1].size) return -5;
        a.buffers[1].output_size = count;
    }
    if (number == SYS_GETSOCKOPT) {
        size_t count = *reinterpret_cast<uint32_t*>(a.buffers[4].data);
        if (count < a.buffers[3].output_size) a.buffers[3].output_size = count;
    }
    for (auto& b : a.buffers) {
        if (b.output_size && !memory::copy_to_user(b.user, b.data, b.output_size)) return EFAULT_RESULT;
    }
    return result;
}
} // namespace kernel
