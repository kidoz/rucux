// SPDX-License-Identifier: MIT
#include <sys/reboot.h>
#include <uapi/kernel/syscalls.h>

#include "syscall_impl.h"

extern "C" {

int reboot(int howto) {
    return static_cast<int>(__syscall(SYS_POWER_CTL, static_cast<long>(howto)));
}

} // extern "C"

