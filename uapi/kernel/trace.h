// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

#define TRACE_SNAPSHOT_MAGIC 0x54525543u
#define TRACE_SNAPSHOT_VERSION 1u

#define TRACE_CTL_RESET    0u
#define TRACE_CTL_ENABLE   1u
#define TRACE_CTL_DISABLE  2u
#define TRACE_CTL_SNAPSHOT 3u
#define TRACE_CTL_STATS    4u

#define TRACE_CLOCK_NONE       0u
#define TRACE_CLOCK_TSC_RAW    1u
#define TRACE_CLOCK_CNTVCT_RAW 2u

#define TRACE_RECORD_INSTANT    1u
#define TRACE_RECORD_SLICE_BEGIN 2u
#define TRACE_RECORD_SLICE_END  3u
#define TRACE_RECORD_COUNTER    4u

#define TRACE_EVENT_BOOT_STAGE      1u
#define TRACE_EVENT_THREAD_ENQUEUE  2u
#define TRACE_EVENT_SCHED_SWITCH    3u
#define TRACE_EVENT_THREAD_BLOCK    4u
#define TRACE_EVENT_THREAD_EXIT     5u
#define TRACE_EVENT_IRQ_TIMER       6u
#define TRACE_EVENT_IRQ_WAKE        7u
#define TRACE_EVENT_SYSCALL_ENTER   8u
#define TRACE_EVENT_SYSCALL_EXIT    9u
#define TRACE_EVENT_PAGE_FAULT      10u
#define TRACE_EVENT_TRACE_CTL       11u
#define TRACE_EVENT_IPC_SEND_SYNC   12u
#define TRACE_EVENT_IPC_SEND_ASYNC  13u
#define TRACE_EVENT_IPC_RECV_SYNC   14u
#define TRACE_EVENT_IPC_CALL        15u
#define TRACE_EVENT_IPC_REPLY       16u
#define TRACE_EVENT_IPC_WAIT        17u
#define TRACE_EVENT_VFS_OPEN        18u
#define TRACE_EVENT_VFS_READ        19u
#define TRACE_EVENT_VFS_WRITE       20u
#define TRACE_EVENT_VFS_CLOSE       21u
#define TRACE_EVENT_CRASH           22u

struct trace_record {
    uint64_t timestamp;
    uint64_t seq_no;
    uint64_t arg0;
    uint64_t arg1;
    uint32_t cpu_id;
    uint32_t thread_id;
    uint16_t type;
    uint16_t event;
    uint32_t reserved;
};

struct trace_snapshot_header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t clock_id;
    uint32_t cpu_count;
    uint32_t record_size;
    uint32_t record_count;
    uint64_t clock_freq_hz;
    uint64_t records_written;
    uint64_t records_overwritten;
};

struct trace_stats {
    uint32_t enabled;
    uint32_t clock_id;
    uint32_t cpu_count;
    uint32_t record_capacity_per_cpu;
    uint64_t clock_freq_hz;
    uint64_t records_written;
    uint64_t records_overwritten;
    uint64_t records_available;
};
