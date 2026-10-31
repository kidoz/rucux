// SPDX-License-Identifier: MIT
//
// Minimal AArch64 userspace program. Its job is to prove the EL0 round trip:
// ERET into userspace, SVC back into the kernel, and a return value delivered
// through x0. It deliberately avoids libc so a failure points at the syscall
// path rather than at library code.

#include <uapi/kernel/syscalls.h>
#include <unistd.h>

namespace {

constexpr const char MESSAGE[] = "userspace: hello from EL0\n";

// sizeof includes the NUL terminator, which must not be written.
constexpr unsigned long MESSAGE_LEN = sizeof(MESSAGE) - 1;

} // namespace

int main() {
    ssize_t written = write(1, MESSAGE, MESSAGE_LEN);

    // A short write means the syscall returned but the ABI disagrees about
    // argument or return registers — worth distinguishing from silence.
    if (written != static_cast<ssize_t>(MESSAGE_LEN)) {
        const char err[] = "userspace: short write\n";
        write(1, err, sizeof(err) - 1);
        return 1;
    }

    volatile unsigned long value = 0x12345678;
    for (int i = 0; i < 200; ++i) {
        syscall(SYS_YIELD);
        for (volatile unsigned spin = 0; spin < 5000; spin = spin + 1) {
        }
        if (value != 0x12345678) return 1;
    }
    const char done[] = "userspace: scheduling verified\n";
    write(1, done, sizeof(done) - 1);
    return 0;
}
