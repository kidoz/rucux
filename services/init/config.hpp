// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

struct service_config {
    const char* name;
    const char* path;
    bool autostart;
    bool restart_on_failure;
    bool restart_on_success;
    uint32_t max_restart_attempts;
    uint32_t restart_delay_ms;
    const char* dependencies[3];
    uint32_t dependency_count;
};

#include <generated_init_config.hpp>
