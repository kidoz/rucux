// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define INITD_SERVICE_NAME_MAX 16

#define INITD_MSG_REGISTER_SERVICE 10
#define INITD_MSG_LOOKUP_SERVICE   11
#define INITD_MSG_START_SERVICE    12
#define INITD_MSG_STOP_SERVICE     13
#define INITD_MSG_STATUS_SERVICE   14
#define INITD_MSG_CONTROL_REPLY    15

enum initd_service_state : uint32_t {
    INITD_SERVICE_UNKNOWN  = 0,
    INITD_SERVICE_STARTING = 1,
    INITD_SERVICE_RUNNING  = 2,
    INITD_SERVICE_STOPPING = 3,
    INITD_SERVICE_STOPPED  = 4,
    INITD_SERVICE_FAILED   = 5,
    INITD_SERVICE_WAITING  = 6,
};

enum initd_control_result : uint32_t {
    INITD_CTL_OK      = 0,
    INITD_CTL_ENOENT  = 1,
    INITD_CTL_EBUSY   = 2,
    INITD_CTL_EFAIL   = 3,
    INITD_CTL_EPERM   = 4,
};
