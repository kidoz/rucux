// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

struct top_process_info {
    uint32_t tid;
    uint32_t state; // 0=READY, 1=RUNNING, 2=BLOCKED, 3=TERMINATED
    uint8_t priority;
    uint32_t cpu;
};

struct top_info {
    uint32_t num_processes;
    struct top_process_info processes[64];
};