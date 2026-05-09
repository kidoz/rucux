// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

enum rucux_capability : uint32_t {
    RUCUX_CAP_SPAWN_AS = 1u << 0,
    RUCUX_CAP_DISPLAY_ADMIN = 1u << 1,
    RUCUX_CAP_INPUT_ADMIN = 1u << 2,
    RUCUX_CAP_SESSION_ADMIN = 1u << 3,
    RUCUX_CAP_POWER = 1u << 4,
};

struct rucux_credentials {
    uint32_t ruid;
    uint32_t euid;
    uint32_t suid;
    uint32_t rgid;
    uint32_t egid;
    uint32_t sgid;
    uint32_t sid;
    uint32_t pgid;
    uint32_t capabilities;
};
