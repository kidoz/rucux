// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

static int fail() {
    const char text[] = "syscall safety: FAIL\n";
    write(1, text, sizeof(text) - 1);
    return 1;
}

int main() {
    if (syscall(SYS_WRITE, 1, 0, 8) != -14 || syscall(SYS_WRITE, 1, 0x40000000, 8) != -14 ||
        syscall(SYS_OPEN, 0) != -14 || syscall(SYS_IPC_RECV, 0) != -14 || syscall(SYS_IPC_SEND, 1, 0) != -14 ||
        syscall(SYS_FSTAT, 1, 0) != -14)
        return fail();
    // A user-readable text page must not be writable via a kernel output copy.
    if (syscall(SYS_FSTAT, 1, reinterpret_cast<long>(&main)) != -14) return fail();
    struct {
        struct stat value;
        uint64_t canary[3];
    } result{};
    for (auto& c : result.canary)
        c = 0x123456789ABCDEF0ULL;
    if (syscall(SYS_FSTAT, 1, reinterpret_cast<long>(&result.value)) != 0 ||
        (result.value.st_mode & 0170000) != 0020000)
        return fail();
    for (auto c : result.canary)
        if (c != 0x123456789ABCDEF0ULL) return fail();
    if (mmap(reinterpret_cast<void*>(0x40000000), 4096, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
             -1, 0) != MAP_FAILED)
        return fail();

    // Keep a process-specific stack value live over repeated scheduling and
    // timer interrupts while another process executes at a different EL0 PC.
    volatile uint64_t value = 0x3141592653589793ULL;
    for (int i = 0; i < 200; ++i) {
        syscall(SYS_YIELD);
        for (volatile unsigned spin = 0; spin < 5000; spin = spin + 1) {
        }
        if (value != 0x3141592653589793ULL) return fail();
    }
    const char text[] = "syscall safety: PASS\n";
    write(1, text, sizeof(text) - 1);
    return 0;
}
