// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define TRACE_PRODUCER_MAGIC 0x54525044u
#define TRACE_PRODUCER_VERSION 1u

#define TRACE_PRODUCER_SLOT_COUNT 32u
#define TRACE_PRODUCER_BUFFER_BYTES 4096u
#define TRACE_PRODUCER_FILE_PREFIX "/run/trace.p"
#define TRACE_PRODUCER_PATH_MAX 32u

#define TRACE_PRODUCER_EVENT_CONSOLE_RX 0x1001u
#define TRACE_PRODUCER_EVENT_CONSOLE_TX 0x1002u
#define TRACE_PRODUCER_EVENT_NET_REQUEST 0x2001u
#define TRACE_PRODUCER_EVENT_NET_RESPONSE 0x2002u

struct trace_producer_buffer_header {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    uint32_t producer_id;
    uint32_t category;
    uint32_t session_generation;
    uint32_t write_offset;
    uint32_t record_count;
    uint32_t dropped_records;
    uint32_t wrapped;
    uint64_t next_seq;
};

struct trace_producer_record {
    uint64_t timestamp_ns;
    uint64_t seq_no;
    uint64_t arg0;
    uint64_t arg1;
    uint32_t producer_id;
    uint16_t event;
    uint16_t reserved0;
};
