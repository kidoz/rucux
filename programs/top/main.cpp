// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/top.h>
#include <unistd.h>

static const char* state_to_str(uint32_t state) {
    switch (state) {
    case 0:
        return "READY";
    case 1:
        return "RUNNING";
    case 2:
        return "BLOCKED";
    case 3:
        return "TERMINAT";
    default:
        return "UNKNOWN";
    }
}

int main() {
    struct top_info info;

    int ret = syscall(SYS_TOP, (long)&info, sizeof(info));
    if (ret < 0) {
        printf("top: sys_top failed\n");
        return 1;
    }

    printf("\n  TID | STATE    | PRIO | CPU\n");
    printf("--------------------------------\n");

    for (uint32_t i = 0; i < info.num_processes; ++i) {
        printf(" %4d | %-8s | %4d | %3d\n", info.processes[i].tid, state_to_str(info.processes[i].state),
               info.processes[i].priority, info.processes[i].cpu);
    }
    printf("\nTotal threads: %d\n", info.num_processes);

    return 0;
}
