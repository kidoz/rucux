// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/trace.h>
#include <sys/trace_producer.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/trace_producer.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/traced.h>
#include <uapi/kernel/traced_export.h>
#include <unistd.h>

extern "C" {

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

namespace {

struct traced_session_info {
    bool active;
    uint32_t generation;
    uint32_t producers;
    uint64_t last_snapshot_bytes;
};

struct traced_producer_info {
    uint32_t id;
    uint32_t category;
    uint32_t tid;
    bool session_active;
    uint32_t generation;
    char name[TRACED_PRODUCER_NAME_MAX];
};

void copy_service_name(message& msg, const char* name) {
    char* dst = reinterpret_cast<char*>(msg.data);
    for (int i = 0; i < INITD_SERVICE_NAME_MAX; ++i) {
        dst[i] = name[i];
        if (name[i] == '\0') break;
    }
}

void copy_producer_name(message& msg, const char* name) {
    char* dst = reinterpret_cast<char*>(&msg.data[1]);
    for (uint32_t i = 0; i < TRACED_PRODUCER_NAME_MAX; ++i) {
        dst[i] = name[i];
        if (name[i] == '\0') break;
    }
}

uint32_t low_u32(uint64_t value) {
    return static_cast<uint32_t>(value & 0xffffffffULL);
}

uint32_t high_u32(uint64_t value) {
    return static_cast<uint32_t>((value >> 32) & 0xffffffffULL);
}

static const char* event_name(uint16_t event) {
    switch (event) {
    case TRACE_EVENT_BOOT_STAGE: return "BOOT";
    case TRACE_EVENT_THREAD_ENQUEUE: return "ENQUEUE";
    case TRACE_EVENT_SCHED_SWITCH: return "SWITCH";
    case TRACE_EVENT_THREAD_BLOCK: return "BLOCK";
    case TRACE_EVENT_THREAD_EXIT: return "EXIT";
    case TRACE_EVENT_IRQ_TIMER: return "IRQ_TIMER";
    case TRACE_EVENT_IRQ_WAKE: return "IRQ_WAKE";
    case TRACE_EVENT_SYSCALL_ENTER: return "SYSCALL_IN";
    case TRACE_EVENT_SYSCALL_EXIT: return "SYSCALL_OUT";
    case TRACE_EVENT_PAGE_FAULT: return "PAGE_FAULT";
    case TRACE_EVENT_TRACE_CTL: return "TRACE_CTL";
    case TRACE_EVENT_IPC_SEND_SYNC: return "IPC_SEND_SYNC";
    case TRACE_EVENT_IPC_SEND_ASYNC: return "IPC_SEND_ASYNC";
    case TRACE_EVENT_IPC_RECV_SYNC: return "IPC_RECV_SYNC";
    case TRACE_EVENT_IPC_CALL: return "IPC_CALL";
    case TRACE_EVENT_IPC_REPLY: return "IPC_REPLY";
    case TRACE_EVENT_IPC_WAIT: return "IPC_WAIT";
    case TRACE_EVENT_VFS_OPEN: return "VFS_OPEN";
    case TRACE_EVENT_VFS_READ: return "VFS_READ";
    case TRACE_EVENT_VFS_WRITE: return "VFS_WRITE";
    case TRACE_EVENT_VFS_CLOSE: return "VFS_CLOSE";
    case TRACE_EVENT_CRASH: return "CRASH";
    default: return "UNKNOWN";
    }
}

static const char* clock_name(uint32_t clock_id) {
    switch (clock_id) {
    case TRACE_CLOCK_TSC_RAW: return "tsc_raw";
    case TRACE_CLOCK_CNTVCT_RAW: return "cntvct_raw";
    default: return "none";
    }
}

static const char* category_name(uint32_t category) {
    switch (category) {
    case TRACED_PRODUCER_CATEGORY_SERVICE: return "service";
    case TRACED_PRODUCER_CATEGORY_DRIVER: return "driver";
    case TRACED_PRODUCER_CATEGORY_APP: return "app";
    default: return "generic";
    }
}

static const char* export_clock_name(uint32_t clock_id) {
    switch (clock_id) {
    case TRACED_EXPORT_CLOCK_KERNEL_RAW: return "kernel-raw";
    case TRACED_EXPORT_CLOCK_MONOTONIC_NS: return "monotonic-ns";
    default: return "unknown";
    }
}

static const char* producer_event_name(uint16_t event) {
    switch (event) {
    case TRACE_PRODUCER_EVENT_CONSOLE_RX: return "CONSOLE_RX";
    case TRACE_PRODUCER_EVENT_CONSOLE_TX: return "CONSOLE_TX";
    case TRACE_PRODUCER_EVENT_NET_REQUEST: return "NET_REQ";
    case TRACE_PRODUCER_EVENT_NET_RESPONSE: return "NET_RESP";
    default: return "UPROD_UNKNOWN";
    }
}

bool lookup_service_tid(const char* name, uint32_t* tid_out) {
    if (!tid_out) return false;

    message req = {};
    req.type = INITD_MSG_LOOKUP_SERVICE;
    copy_service_name(req, name);
    syscall(SYS_IPC_SEND, 1, reinterpret_cast<long>(&req));

    message resp = {};
    syscall(SYS_IPC_RECV, reinterpret_cast<long>(&resp), 0, 0);
    if (resp.type != INITD_MSG_LOOKUP_SERVICE) return false;

    *tid_out = static_cast<uint32_t>(resp.data[0]);
    return *tid_out != 0;
}

bool traced_request(uint32_t traced_tid, const message& req, message& reply) {
    syscall(SYS_IPC_SEND, traced_tid, reinterpret_cast<long>(&req));
    syscall(SYS_IPC_RECV, reinterpret_cast<long>(&reply), 0, 0);
    return true;
}

bool traced_control(uint32_t traced_tid, uint32_t type, traced_session_info* session_info = nullptr) {
    message req = {};
    req.type = type;

    message reply = {};
    traced_request(traced_tid, req, reply);
    if (reply.type != TRACED_MSG_CONTROL_REPLY || reply.data[0] != TRACED_CTL_OK) return false;

    if (session_info) {
        session_info->generation = low_u32(reply.data[1]);
        session_info->active = high_u32(reply.data[1]) != 0;
        session_info->producers = static_cast<uint32_t>(reply.data[2]);
        session_info->last_snapshot_bytes = 0;
    }
    return true;
}

bool traced_fill_stats(uint32_t traced_tid, trace_stats* stats) {
    if (!stats) return false;

    message info_req = {};
    info_req.type = TRACED_MSG_GET_INFO;
    message info_reply = {};
    traced_request(traced_tid, info_req, info_reply);
    if (info_reply.type != TRACED_MSG_INFO_REPLY || info_reply.data[0] != TRACED_CTL_OK) return false;

    message counters_req = {};
    counters_req.type = TRACED_MSG_GET_COUNTERS;
    message counters_reply = {};
    traced_request(traced_tid, counters_req, counters_reply);
    if (counters_reply.type != TRACED_MSG_COUNTERS_REPLY || counters_reply.data[0] != TRACED_CTL_OK) return false;

    memset(stats, 0, sizeof(*stats));
    stats->enabled = low_u32(info_reply.data[1]);
    stats->clock_id = high_u32(info_reply.data[1]);
    stats->cpu_count = low_u32(info_reply.data[2]);
    stats->record_capacity_per_cpu = high_u32(info_reply.data[2]);
    stats->clock_freq_hz = info_reply.data[3];
    stats->records_written = counters_reply.data[1];
    stats->records_overwritten = counters_reply.data[2];
    stats->records_available = counters_reply.data[3];
    return true;
}

bool traced_get_session(uint32_t traced_tid, traced_session_info* session_info) {
    if (!session_info) return false;

    message req = {};
    req.type = TRACED_MSG_GET_SESSION;
    message reply = {};
    traced_request(traced_tid, req, reply);
    if (reply.type != TRACED_MSG_SESSION_REPLY || reply.data[0] != TRACED_CTL_OK) return false;

    session_info->generation = low_u32(reply.data[1]);
    session_info->active = high_u32(reply.data[1]) != 0;
    session_info->producers = static_cast<uint32_t>(reply.data[2]);
    session_info->last_snapshot_bytes = reply.data[3];
    return true;
}

bool traced_register_producer(uint32_t traced_tid, const char* name, uint32_t category, traced_producer_info* producer_info) {
    if (!name || !producer_info) return false;

    message req = {};
    req.type = TRACED_MSG_REGISTER_PRODUCER;
    req.data[0] = category;
    copy_producer_name(req, name);

    message reply = {};
    traced_request(traced_tid, req, reply);
    if (reply.type != TRACED_MSG_PRODUCER_REPLY || reply.data[0] != TRACED_CTL_OK) return false;

    producer_info->id = static_cast<uint32_t>(reply.data[1]);
    producer_info->generation = low_u32(reply.data[2]);
    producer_info->session_active = high_u32(reply.data[2]) != 0;
    producer_info->category = static_cast<uint32_t>(reply.data[3]);
    return true;
}

bool traced_get_producer(uint32_t traced_tid, uint32_t index, traced_producer_info* producer_info) {
    if (!producer_info) return false;

    message req = {};
    req.type = TRACED_MSG_GET_PRODUCER;
    req.data[0] = index;

    message reply = {};
    traced_request(traced_tid, req, reply);
    if (reply.type != TRACED_MSG_PRODUCER_INFO_REPLY) return false;
    if (low_u32(reply.data[0]) != TRACED_CTL_OK) return false;

    memset(producer_info, 0, sizeof(*producer_info));
    producer_info->id = high_u32(reply.data[0]);
    producer_info->category = low_u32(reply.data[1]);
    producer_info->tid = high_u32(reply.data[1]);
    memcpy(producer_info->name, &reply.data[2], sizeof(reply.data[2]) + sizeof(reply.data[3]));
    producer_info->name[TRACED_PRODUCER_NAME_MAX - 1] = '\0';
    return true;
}

bool traced_capture_snapshot(uint32_t traced_tid, void* buffer, size_t size, ssize_t* bytes_out) {
    if (!buffer || !bytes_out) return false;

    message req = {};
    req.type = TRACED_MSG_CAPTURE_SNAPSHOT;
    message reply = {};
    traced_request(traced_tid, req, reply);
    if (reply.type != TRACED_MSG_SNAPSHOT_REPLY || reply.data[0] != TRACED_CTL_OK) return false;

    ssize_t bytes = static_cast<ssize_t>(reply.data[1]);
    if (bytes <= 0 || static_cast<size_t>(bytes) > size) return false;

    int fd = open(TRACED_SNAPSHOT_PATH, O_RDONLY);
    if (fd < 0) return false;

    size_t total = 0;
    while (total < static_cast<size_t>(bytes)) {
        ssize_t got = read(fd, static_cast<char*>(buffer) + total, static_cast<size_t>(bytes) - total);
        if (got <= 0) {
            close(fd);
            return false;
        }
        total += static_cast<size_t>(got);
    }

    close(fd);
    *bytes_out = bytes;
    return true;
}

bool traced_capture_export(uint32_t traced_tid, void* buffer, size_t size, ssize_t* bytes_out) {
    if (!buffer || !bytes_out) return false;

    message req = {};
    req.type = TRACED_MSG_CAPTURE_EXPORT;
    message reply = {};
    traced_request(traced_tid, req, reply);
    if (reply.type != TRACED_MSG_EXPORT_REPLY || reply.data[0] != TRACED_CTL_OK) return false;

    ssize_t bytes = static_cast<ssize_t>(reply.data[1]);
    if (bytes <= 0 || static_cast<size_t>(bytes) > size) return false;

    int fd = open(TRACED_EXPORT_PATH, O_RDONLY);
    if (fd < 0) return false;

    size_t total = 0;
    while (total < static_cast<size_t>(bytes)) {
        ssize_t got = read(fd, static_cast<char*>(buffer) + total, static_cast<size_t>(bytes) - total);
        if (got <= 0) {
            close(fd);
            return false;
        }
        total += static_cast<size_t>(got);
    }

    close(fd);
    *bytes_out = bytes;
    return true;
}

bool acquire_stats(trace_stats* stats, bool* used_traced) {
    if (used_traced) *used_traced = false;

    uint32_t traced_tid = 0;
    if (lookup_service_tid("traced", &traced_tid) && traced_fill_stats(traced_tid, stats)) {
        if (used_traced) *used_traced = true;
        return true;
    }

    return trace_get_stats(stats) == 0;
}

void print_status(const trace_stats& stats, bool via_traced, const traced_session_info* session_info) {
    printf("trace status\n");
    printf("  control=%s snapshot=%s\n",
           via_traced ? "traced" : "kernel-direct",
           via_traced ? "traced-file" : "kernel-direct");
    if (session_info) {
        printf("  session=%s generation=%u producers=%u last_snapshot=%llu\n",
               session_info->active ? "active" : "stopped",
               session_info->generation,
               session_info->producers,
               (unsigned long long)session_info->last_snapshot_bytes);
    }
    printf("  clock=%s freq=%llu enabled=%u cpus=%u capacity/cpu=%u\n",
           clock_name(stats.clock_id),
           (unsigned long long)stats.clock_freq_hz,
           stats.enabled,
           stats.cpu_count,
           stats.record_capacity_per_cpu);
    printf("  written=%llu available=%llu overwritten=%llu\n",
           (unsigned long long)stats.records_written,
           (unsigned long long)stats.records_available,
           (unsigned long long)stats.records_overwritten);
}

int dump_snapshot(bool raw_output) {
    trace_stats stats = {};
    bool via_traced = false;
    if (!acquire_stats(&stats, &via_traced)) {
        printf("trace: failed to acquire trace stats\n");
        return 1;
    }

    size_t snapshot_size = sizeof(trace_snapshot_header) + (stats.records_available + 32) * sizeof(trace_record);
    if (snapshot_size < (128 * 1024)) snapshot_size = 128 * 1024;
    if (snapshot_size > (2 * 1024 * 1024)) snapshot_size = 2 * 1024 * 1024;

    void* buffer = mmap(nullptr, snapshot_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        printf("trace: failed to allocate snapshot buffer\n");
        return 1;
    }

    ssize_t bytes = -1;
    uint32_t traced_tid = 0;
    if (lookup_service_tid("traced", &traced_tid)) {
        if (traced_capture_snapshot(traced_tid, buffer, snapshot_size, &bytes)) {
            via_traced = true;
        }
    }

    if (bytes < 0) {
        via_traced = false;
        bytes = trace_snapshot(buffer, snapshot_size);
        if (bytes < 0) {
            printf("trace: trace_snapshot failed\n");
            return 1;
        }
    }

    auto* header = static_cast<trace_snapshot_header*>(buffer);
    if (header->magic != TRACE_SNAPSHOT_MAGIC) {
        printf("trace: invalid snapshot magic\n");
        return 1;
    }

    if (raw_output) {
        ssize_t written = write(1, buffer, static_cast<size_t>(bytes));
        return (written == bytes) ? 0 : 1;
    }

    auto* records = reinterpret_cast<trace_record*>(header + 1);

    printf("trace snapshot\n");
    printf("  control=%s snapshot=%s\n",
           via_traced ? "traced" : "kernel-direct",
           via_traced ? "traced-file" : "kernel-direct");
    printf("  clock=%s freq=%llu enabled=%u cpus=%u\n",
           clock_name(stats.clock_id),
           (unsigned long long)stats.clock_freq_hz,
           stats.enabled,
           stats.cpu_count);
    printf("  written=%llu available=%llu overwritten=%llu dumped=%u bytes=%d\n",
           (unsigned long long)header->records_written,
           (unsigned long long)stats.records_available,
           (unsigned long long)header->records_overwritten,
           header->record_count,
           (int)bytes);

    uint32_t start = 0;
    if (header->record_count > 64) start = header->record_count - 64;

    printf("\n last %u records\n", header->record_count - start);
    printf("  seq | cpu | tid | event      | arg0               | arg1               | ts\n");
    printf("-------------------------------------------------------------------------------\n");

    for (uint32_t i = start; i < header->record_count; ++i) {
        const trace_record& rec = records[i];
        printf(" %4llu | %3u | %3u | %-10s | 0x%016llx | 0x%016llx | 0x%016llx\n",
               (unsigned long long)rec.seq_no,
               rec.cpu_id,
               rec.thread_id,
               event_name(rec.event),
               (unsigned long long)rec.arg0,
               (unsigned long long)rec.arg1,
               (unsigned long long)rec.timestamp);
    }

    return 0;
}

int dump_export(bool raw_output) {
    uint32_t traced_tid = 0;
    if (!lookup_service_tid("traced", &traced_tid)) {
        return dump_snapshot(raw_output);
    }

    size_t export_size = sizeof(traced_export_header) +
                         sizeof(traced_export_section_header) + (2 * 1024 * 1024) +
                         TRACE_PRODUCER_SLOT_COUNT *
                             (sizeof(traced_export_section_header) + TRACE_PRODUCER_BUFFER_BYTES);
    if (export_size < (512 * 1024)) export_size = 512 * 1024;

    void* buffer = mmap(nullptr, export_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        printf("trace: failed to allocate export buffer\n");
        return 1;
    }

    ssize_t bytes = -1;
    if (!traced_capture_export(traced_tid, buffer, export_size, &bytes)) {
        return dump_snapshot(raw_output);
    }

    if (raw_output) {
        ssize_t written = write(1, buffer, static_cast<size_t>(bytes));
        return (written == bytes) ? 0 : 1;
    }

    auto* export_header = static_cast<traced_export_header*>(buffer);
    if (export_header->magic != TRACED_EXPORT_MAGIC || export_header->version != TRACED_EXPORT_VERSION) {
        printf("trace: invalid export header\n");
        return 1;
    }

    printf("trace export\n");
    printf("  sections=%u bytes=%llu\n",
           export_header->section_count,
           (unsigned long long)export_header->total_size);

    uint8_t* cursor = static_cast<uint8_t*>(buffer) + export_header->header_size;
    uint8_t* end = static_cast<uint8_t*>(buffer) + bytes;

    for (uint32_t section_index = 0; section_index < export_header->section_count; ++section_index) {
        if (cursor + sizeof(traced_export_section_header) > end) {
            printf("trace: truncated export section header\n");
            return 1;
        }

        auto* section = reinterpret_cast<traced_export_section_header*>(cursor);
        if (section->section_size < sizeof(traced_export_section_header) || cursor + section->section_size > end) {
            printf("trace: invalid export section size\n");
            return 1;
        }

        uint8_t* payload = cursor + sizeof(traced_export_section_header);

        if (section->type == TRACED_EXPORT_SECTION_KERNEL_SNAPSHOT) {
            auto* header = reinterpret_cast<trace_snapshot_header*>(payload);
            if (section->section_size < sizeof(traced_export_section_header) + sizeof(trace_snapshot_header) ||
                header->magic != TRACE_SNAPSHOT_MAGIC) {
                printf("trace: invalid kernel section\n");
                return 1;
            }

            auto* records = reinterpret_cast<trace_record*>(header + 1);
            printf("\n kernel section\n");
            printf("  clock=%s exported-clock=%s freq=%llu cpus=%u records=%u\n",
                   clock_name(header->clock_id),
                   export_clock_name(section->clock_id),
                   (unsigned long long)header->clock_freq_hz,
                   header->cpu_count,
                   header->record_count);

            uint32_t start = 0;
            if (header->record_count > 64) start = header->record_count - 64;

            printf("  seq | cpu | tid | event      | arg0               | arg1               | ts\n");
            printf("-------------------------------------------------------------------------------\n");
            for (uint32_t i = start; i < header->record_count; ++i) {
                const trace_record& rec = records[i];
                printf(" %4llu | %3u | %3u | %-10s | 0x%016llx | 0x%016llx | 0x%016llx\n",
                       (unsigned long long)rec.seq_no,
                       rec.cpu_id,
                       rec.thread_id,
                       event_name(rec.event),
                       (unsigned long long)rec.arg0,
                       (unsigned long long)rec.arg1,
                       (unsigned long long)rec.timestamp);
            }
        } else if (section->type == TRACED_EXPORT_SECTION_PRODUCER_RECORDS) {
            auto* records = reinterpret_cast<trace_producer_record*>(payload);
            printf("\n producer section\n");
            printf("  id=%u name=%s category=%s tid=%u clock=%s records=%u\n",
                   section->producer_id,
                   section->name,
                   category_name(section->category),
                   section->tid,
                   export_clock_name(section->clock_id),
                   section->record_count);
            printf("  seq | event         | arg0               | arg1               | ts(ns)\n");
            printf("----------------------------------------------------------------------------\n");
            for (uint32_t i = 0; i < section->record_count; ++i) {
                const trace_producer_record& rec = records[i];
                printf(" %4llu | %-13s | 0x%016llx | 0x%016llx | %llu\n",
                       (unsigned long long)rec.seq_no,
                       producer_event_name(rec.event),
                       (unsigned long long)rec.arg0,
                       (unsigned long long)rec.arg1,
                       (unsigned long long)rec.timestamp_ns);
            }
        }

        cursor += section->section_size;
    }

    return 0;
}

int print_producers(uint32_t traced_tid) {
    traced_session_info session_info = {};
    if (!traced_get_session(traced_tid, &session_info)) {
        printf("trace: failed to query traced session\n");
        return 1;
    }

    printf("trace producers\n");
    printf("  session=%s generation=%u count=%u\n",
           session_info.active ? "active" : "stopped",
           session_info.generation,
           session_info.producers);

    for (uint32_t index = 0; index < session_info.producers; ++index) {
        traced_producer_info producer_info = {};
        if (!traced_get_producer(traced_tid, index, &producer_info)) {
            printf("  [%u] <unavailable>\n", index);
            continue;
        }

        printf("  [%u] id=%u name=%s category=%s tid=%u session=%s generation=%u\n",
               index,
               producer_info.id,
               producer_info.name,
               category_name(producer_info.category),
               producer_info.tid,
               session_info.active ? "active" : "stopped",
               session_info.generation);
    }

    return 0;
}

int dump_producer(uint32_t producer_id) {
    char path[TRACE_PRODUCER_PATH_MAX] = {};
    if (trace_producer_format_path(producer_id, path, sizeof(path)) < 0) {
        printf("trace: invalid producer id\n");
        return 1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("trace: failed to open %s\n", path);
        return 1;
    }

    trace_producer_buffer_header header = {};
    if (read(fd, &header, sizeof(header)) != (ssize_t)sizeof(header)) {
        close(fd);
        printf("trace: failed to read producer header\n");
        return 1;
    }

    if (header.magic != TRACE_PRODUCER_MAGIC || header.version != TRACE_PRODUCER_VERSION) {
        close(fd);
        printf("trace: invalid producer buffer\n");
        return 1;
    }

    constexpr size_t header_size = sizeof(trace_producer_buffer_header);
    constexpr size_t record_size = sizeof(trace_producer_record);
    constexpr size_t max_records = (TRACE_PRODUCER_BUFFER_BYTES - header_size) / record_size;

    uint32_t available_records = 0;
    if (header.wrapped) {
        available_records = static_cast<uint32_t>(max_records);
    } else if (header.write_offset >= header_size) {
        available_records = static_cast<uint32_t>((header.write_offset - header_size) / record_size);
    }
    if (available_records > header.record_count) available_records = header.record_count;

    trace_producer_record records[max_records] = {};
    uint32_t loaded = 0;
    if (!header.wrapped) {
        if (available_records != 0) {
            if (lseek(fd, (long)header_size, SEEK_SET) < 0) {
                close(fd);
                return 1;
            }
            size_t bytes = available_records * record_size;
            if (read(fd, records, bytes) != (ssize_t)bytes) {
                close(fd);
                printf("trace: failed to read producer records\n");
                return 1;
            }
            loaded = available_records;
        }
    } else {
        uint32_t tail_records = static_cast<uint32_t>((TRACE_PRODUCER_BUFFER_BYTES - header.write_offset) / record_size);
        if (tail_records > available_records) tail_records = available_records;
        if (tail_records != 0) {
            if (lseek(fd, (long)header.write_offset, SEEK_SET) < 0) {
                close(fd);
                return 1;
            }
            size_t bytes = tail_records * record_size;
            if (read(fd, records, bytes) != (ssize_t)bytes) {
                close(fd);
                printf("trace: failed to read producer tail records\n");
                return 1;
            }
            loaded = tail_records;
        }

        uint32_t head_records = available_records - tail_records;
        if (head_records != 0) {
            if (lseek(fd, (long)header_size, SEEK_SET) < 0) {
                close(fd);
                return 1;
            }
            size_t bytes = head_records * record_size;
            if (read(fd, records + loaded, bytes) != (ssize_t)bytes) {
                close(fd);
                printf("trace: failed to read producer head records\n");
                return 1;
            }
            loaded += head_records;
        }
    }

    close(fd);

    printf("producer %u\n", producer_id);
    printf("  category=%s generation=%u wrapped=%u records=%u dropped=%u\n",
           category_name(header.category),
           header.session_generation,
           header.wrapped,
           loaded,
           header.dropped_records);
    printf("  seq | event         | arg0               | arg1               | ts(ns)\n");
    printf("----------------------------------------------------------------------------\n");

    for (uint32_t i = 0; i < loaded; ++i) {
        const trace_producer_record& rec = records[i];
        printf(" %4llu | %-13s | 0x%016llx | 0x%016llx | %llu\n",
               (unsigned long long)rec.seq_no,
               producer_event_name(rec.event),
               (unsigned long long)rec.arg0,
               (unsigned long long)rec.arg1,
               (unsigned long long)rec.timestamp_ns);
    }

    return 0;
}

void print_usage() {
    printf("usage: trace [status|start|stop|enable|disable|reset|dump|producers|producer-dump <id>|register <name> [category]] [--raw]\n");
}

uint32_t parse_category(const char* arg) {
    if (!arg) return TRACED_PRODUCER_CATEGORY_GENERIC;
    if (strcmp(arg, "service") == 0) return TRACED_PRODUCER_CATEGORY_SERVICE;
    if (strcmp(arg, "driver") == 0) return TRACED_PRODUCER_CATEGORY_DRIVER;
    if (strcmp(arg, "app") == 0) return TRACED_PRODUCER_CATEGORY_APP;
    return static_cast<uint32_t>(strtoul(arg, nullptr, 0));
}

} // namespace

int main(int argc, char** argv) {
    const char* command = "dump";
    const char* arg1 = nullptr;
    const char* arg2 = nullptr;
    bool raw_output = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--raw") == 0) {
            raw_output = true;
            continue;
        }

        if (strcmp(command, "dump") == 0) {
            command = argv[i];
            continue;
        }

        if (!arg1) {
            arg1 = argv[i];
            continue;
        }

        if (!arg2) {
            arg2 = argv[i];
            continue;
        }

        print_usage();
        return 1;
    }

    if (strcmp(command, "dump") == 0) {
        return dump_export(raw_output);
    }

    if (raw_output) {
        print_usage();
        return 1;
    }

    uint32_t traced_tid = 0;
    if (!lookup_service_tid("traced", &traced_tid)) {
        printf("trace: traced service is not available\n");
        return 1;
    }

    if (strcmp(command, "status") == 0) {
        trace_stats stats = {};
        traced_session_info session_info = {};
        if (!traced_fill_stats(traced_tid, &stats) || !traced_get_session(traced_tid, &session_info)) {
            printf("trace: failed to query traced status\n");
            return 1;
        }
        print_status(stats, true, &session_info);
        return 0;
    }

    if (strcmp(command, "producers") == 0) {
        return print_producers(traced_tid);
    }

    if (strcmp(command, "producer-dump") == 0) {
        if (!arg1) {
            print_usage();
            return 1;
        }
        return dump_producer(static_cast<uint32_t>(strtoul(arg1, nullptr, 0)));
    }

    if (strcmp(command, "register") == 0) {
        if (!arg1) {
            print_usage();
            return 1;
        }

        traced_producer_info producer_info = {};
        uint32_t category = parse_category(arg2);
        if (!traced_register_producer(traced_tid, arg1, category, &producer_info)) {
            printf("trace: register failed\n");
            return 1;
        }

        printf("trace: producer %s registered id=%u category=%s generation=%u session=%s\n",
               arg1,
               producer_info.id,
               category_name(producer_info.category),
               producer_info.generation,
               producer_info.session_active ? "active" : "stopped");
        return 0;
    }

    traced_session_info session_info = {};
    uint32_t request = 0;
    if (strcmp(command, "start") == 0 || strcmp(command, "enable") == 0) {
        request = TRACED_MSG_START_SESSION;
    } else if (strcmp(command, "stop") == 0 || strcmp(command, "disable") == 0) {
        request = TRACED_MSG_STOP_SESSION;
    } else if (strcmp(command, "reset") == 0) {
        request = TRACED_MSG_RESET_SESSION;
    } else {
        print_usage();
        return 1;
    }

    if (!traced_control(traced_tid, request, &session_info)) {
        printf("trace: %s failed\n", command);
        return 1;
    }

    printf("trace: %s ok (generation=%u session=%s producers=%u)\n",
           command,
           session_info.generation,
           session_info.active ? "active" : "stopped",
           session_info.producers);
    return 0;
}
