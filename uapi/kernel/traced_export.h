// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define TRACED_EXPORT_PATH "/run/trace.export"
#define TRACED_EXPORT_MAGIC 0x54584558u
#define TRACED_EXPORT_VERSION 1u

#define TRACED_EXPORT_SECTION_KERNEL_SNAPSHOT 1u
#define TRACED_EXPORT_SECTION_PRODUCER_RECORDS 2u

#define TRACED_EXPORT_CLOCK_KERNEL_RAW 1u
#define TRACED_EXPORT_CLOCK_MONOTONIC_NS 2u

struct traced_export_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t section_count;
    uint32_t reserved0;
    uint64_t total_size;
};

struct traced_export_section_header {
    uint32_t type;
    uint32_t section_size;
    uint32_t clock_id;
    uint32_t producer_id;
    uint32_t category;
    uint32_t tid;
    uint32_t record_count;
    char name[16];
};
