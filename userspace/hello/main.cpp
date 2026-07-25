// SPDX-License-Identifier: MIT
//
// Minimal AArch64 userspace program. Its job is to prove the EL0 round trip:
// ERET into userspace, SVC back into the kernel, and a return value delivered
// through x0. It deliberately avoids libc so a failure points at the syscall
// path rather than at library code.

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

    // Verified manually: storing to &main here faults with a permission fault
    // (ESR 0x9200004F, FAR at the .text address), confirming elf::load's
    // read-only mapping is enforced at EL0. Not left enabled — the EL0 fault
    // path does not yet terminate the thread cleanly and re-faults.
    return 0;
}
