// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <uapi/kernel/trace_producer.h>
#include <uapi/kernel/traced.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct trace_producer_handle {
    int fd;
    uint32_t producer_id;
    uint32_t category;
    uint32_t session_generation;
    uint32_t write_offset;
    uint32_t dropped_records;
    uint32_t wrapped;
    uint64_t next_seq;
} trace_producer_handle;

int trace_producer_format_path(uint32_t producer_id, char* buffer, size_t size);
int trace_producer_register(const char* name, uint32_t category, trace_producer_handle* handle);
int trace_producer_emit(trace_producer_handle* handle, uint16_t event, uint64_t arg0, uint64_t arg1);
int trace_producer_close(trace_producer_handle* handle);

#ifdef __cplusplus
}
#endif
