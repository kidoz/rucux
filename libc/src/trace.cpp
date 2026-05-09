// SPDX-License-Identifier: MIT
#include <sys/trace.h>
#include <uapi/kernel/syscalls.h>

#include "syscall_impl.h"

extern "C" {

int trace_reset(void) {
    return (int)__syscall(SYS_TRACE_CTL, TRACE_CTL_RESET);
}

int trace_enable(void) {
    return (int)__syscall(SYS_TRACE_CTL, TRACE_CTL_ENABLE);
}

int trace_disable(void) {
    return (int)__syscall(SYS_TRACE_CTL, TRACE_CTL_DISABLE);
}

ssize_t trace_snapshot(void* buffer, size_t size) {
    return (ssize_t)__syscall(SYS_TRACE_CTL, TRACE_CTL_SNAPSHOT, (long)buffer, (long)size);
}

int trace_get_stats(struct trace_stats* stats) {
    if (!stats) return -1;
    return (int)__syscall(SYS_TRACE_CTL, TRACE_CTL_STATS, (long)stats, sizeof(*stats));
}

} // extern "C"
