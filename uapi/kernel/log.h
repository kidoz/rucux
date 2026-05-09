// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

#define RUCUX_LOG_VERSION 1u

#define RUCUX_LOG_CTL_SUBMIT 1u
#define RUCUX_LOG_CTL_CLEAR  2u
#define RUCUX_LOG_CTL_STATS  3u
#define RUCUX_LOG_CTL_READ   4u

#define RUCUX_LOG_EMERG   0u
#define RUCUX_LOG_ALERT   1u
#define RUCUX_LOG_CRIT    2u
#define RUCUX_LOG_ERR     3u
#define RUCUX_LOG_WARNING 4u
#define RUCUX_LOG_NOTICE  5u
#define RUCUX_LOG_INFO    6u
#define RUCUX_LOG_DEBUG   7u

#define RUCUX_LOG_PRIORITY_MASK 0x7u
#define RUCUX_LOG_FACILITY_SHIFT 3u
#define RUCUX_LOG_FACILITY_MASK 0x03f8u

#define RUCUX_LOG_IDENT_MAX 32u
#define RUCUX_LOG_TEXT_MAX 224u

enum rucux_log_source : uint32_t {
    RUCUX_LOG_SOURCE_KERNEL = 1,
    RUCUX_LOG_SOURCE_USER = 2,
};

struct rucux_log_submit_request {
    uint32_t priority;
    uint32_t flags;
    uint32_t ident_length;
    uint32_t message_length;
    const char* ident;
    const char* message;
};

struct rucux_log_stats {
    uint32_t version;
    uint32_t record_capacity;
    uint64_t records_written;
    uint64_t records_visible;
    uint64_t records_overwritten;
};

struct rucux_log_read_request {
    uint64_t start_sequence;
    char* buffer;
    uint32_t buffer_size;
    uint32_t flags;
    uint64_t next_sequence;
    uint64_t dropped_records;
    uint32_t bytes_written;
    uint32_t reserved;
};
