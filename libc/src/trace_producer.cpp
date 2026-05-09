// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/trace_producer.h>
#include <time.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

#include "syscall_impl.h"

extern "C" {

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

static void copy_service_name(message& msg, const char* name) {
    char* dst = reinterpret_cast<char*>(msg.data);
    for (int i = 0; i < INITD_SERVICE_NAME_MAX; ++i) {
        dst[i] = name[i];
        if (name[i] == '\0') break;
    }
}

static void copy_producer_name(message& msg, const char* name) {
    char* dst = reinterpret_cast<char*>(&msg.data[1]);
    for (uint32_t i = 0; i < TRACED_PRODUCER_NAME_MAX; ++i) {
        dst[i] = name[i];
        if (name[i] == '\0') break;
    }
}

static uint32_t low_u32(uint64_t value) {
    return static_cast<uint32_t>(value & 0xffffffffULL);
}

static int lookup_traced_tid(uint32_t* traced_tid) {
    if (!traced_tid) return -1;

    message req = {};
    req.type = INITD_MSG_LOOKUP_SERVICE;
    copy_service_name(req, "traced");
    __syscall(SYS_IPC_SEND, 1, reinterpret_cast<long>(&req));

    message resp = {};
    __syscall(SYS_IPC_RECV, reinterpret_cast<long>(&resp), 0, 0);
    if (resp.type != INITD_MSG_LOOKUP_SERVICE) return -1;

    *traced_tid = static_cast<uint32_t>(resp.data[0]);
    return *traced_tid == 0 ? -1 : 0;
}

static int write_all(int fd, const void* buffer, size_t size) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t total = 0;
    while (total < size) {
        ssize_t written = write(fd, ptr + total, size - total);
        if (written <= 0) return -1;
        total += static_cast<size_t>(written);
    }
    return 0;
}

int trace_producer_format_path(uint32_t producer_id, char* buffer, size_t size) {
    if (!buffer || size == 0 || producer_id == 0 || producer_id > TRACE_PRODUCER_SLOT_COUNT) return -1;
    int written = snprintf(buffer, size, "%s%u", TRACE_PRODUCER_FILE_PREFIX, producer_id);
    if (written <= 0 || static_cast<size_t>(written) >= size) return -1;
    return 0;
}

int trace_producer_register(const char* name, uint32_t category, trace_producer_handle* handle) {
    if (!name || !handle) return -1;

    uint32_t traced_tid = 0;
    if (lookup_traced_tid(&traced_tid) < 0) return -1;

    message req = {};
    req.type = TRACED_MSG_REGISTER_PRODUCER;
    req.data[0] = category;
    copy_producer_name(req, name);
    __syscall(SYS_IPC_SEND, traced_tid, reinterpret_cast<long>(&req));

    message reply = {};
    __syscall(SYS_IPC_RECV, reinterpret_cast<long>(&reply), 0, 0);
    if (reply.type != TRACED_MSG_PRODUCER_REPLY || reply.data[0] != TRACED_CTL_OK) return -1;

    char path[TRACE_PRODUCER_PATH_MAX] = {};
    uint32_t producer_id = static_cast<uint32_t>(reply.data[1]);
    if (trace_producer_format_path(producer_id, path, sizeof(path)) < 0) return -1;

    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;

    trace_producer_buffer_header header = {};
    header.magic = TRACE_PRODUCER_MAGIC;
    header.version = TRACE_PRODUCER_VERSION;
    header.producer_id = producer_id;
    header.category = static_cast<uint32_t>(reply.data[3]);
    header.session_generation = low_u32(reply.data[2]);
    header.write_offset = sizeof(trace_producer_buffer_header);
    header.record_count = 0;
    header.dropped_records = 0;
    header.wrapped = 0;
    header.next_seq = 1;

    if (lseek(fd, 0, SEEK_SET) < 0 || write_all(fd, &header, sizeof(header)) < 0) {
        close(fd);
        return -1;
    }

    handle->fd = fd;
    handle->producer_id = producer_id;
    handle->category = header.category;
    handle->session_generation = header.session_generation;
    handle->write_offset = header.write_offset;
    handle->dropped_records = 0;
    handle->wrapped = 0;
    handle->next_seq = 1;
    return 0;
}

int trace_producer_emit(trace_producer_handle* handle, uint16_t event, uint64_t arg0, uint64_t arg1) {
    if (!handle || handle->fd < 0) return -1;

    trace_producer_buffer_header header = {};
    header.magic = TRACE_PRODUCER_MAGIC;
    header.version = TRACE_PRODUCER_VERSION;
    header.producer_id = handle->producer_id;
    header.category = handle->category;
    header.session_generation = handle->session_generation;
    header.write_offset = handle->write_offset;
    header.record_count = static_cast<uint32_t>(handle->next_seq - 1);
    header.dropped_records = handle->dropped_records;
    header.wrapped = handle->wrapped;
    header.next_seq = handle->next_seq;

    if (header.write_offset < sizeof(trace_producer_buffer_header) || header.write_offset > TRACE_PRODUCER_BUFFER_BYTES) {
        header.write_offset = sizeof(trace_producer_buffer_header);
    }

    if (header.write_offset + sizeof(trace_producer_record) > TRACE_PRODUCER_BUFFER_BYTES) {
        header.write_offset = sizeof(trace_producer_buffer_header);
        header.wrapped = 1;
        header.dropped_records++;
    }

    timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;

    trace_producer_record record = {};
    record.timestamp_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    record.seq_no = handle->next_seq;
    record.arg0 = arg0;
    record.arg1 = arg1;
    record.producer_id = handle->producer_id;
    record.event = event;

    if (lseek(handle->fd, static_cast<long>(header.write_offset), SEEK_SET) < 0 ||
        write_all(handle->fd, &record, sizeof(record)) < 0) {
        return -1;
    }

    handle->next_seq++;
    header.write_offset += sizeof(trace_producer_record);
    header.record_count = static_cast<uint32_t>(handle->next_seq - 1);
    header.next_seq = handle->next_seq;

    if (lseek(handle->fd, 0, SEEK_SET) < 0 || write_all(handle->fd, &header, sizeof(header)) < 0) {
        return -1;
    }

    handle->write_offset = header.write_offset;
    handle->dropped_records = header.dropped_records;
    handle->wrapped = header.wrapped;
    return 0;
}

int trace_producer_close(trace_producer_handle* handle) {
    if (!handle) return -1;
    int rc = 0;
    if (handle->fd >= 0) rc = close(handle->fd);
    handle->fd = -1;
    handle->producer_id = 0;
    handle->category = 0;
    handle->session_generation = 0;
    handle->write_offset = 0;
    handle->dropped_records = 0;
    handle->wrapped = 0;
    handle->next_seq = 0;
    return rc;
}

} // extern "C"
