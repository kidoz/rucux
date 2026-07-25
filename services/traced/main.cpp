// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/trace.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/trace_producer.h>
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

constexpr int MAX_PRODUCERS = 32;

struct producer_entry {
    bool used;
    uint32_t id;
    uint32_t tid;
    uint32_t category;
    char name[TRACED_PRODUCER_NAME_MAX];
};

producer_entry g_producers[MAX_PRODUCERS] = {};
uint32_t g_next_producer_id = 1;
uint32_t g_session_generation = 0;
bool g_session_active = false;
uint64_t g_last_snapshot_bytes = 0;

void copy_bounded_name(char* dst, size_t dst_size, const char* src) {
    size_t i = 0;
    if (dst_size == 0) return;
    for (; i + 1 < dst_size && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

bool names_equal(const char* lhs, const char* rhs) {
    for (size_t i = 0; i < TRACED_PRODUCER_NAME_MAX; ++i) {
        if (lhs[i] != rhs[i]) return false;
        if (lhs[i] == '\0') return true;
    }
    return true;
}

void copy_service_name(message& msg, const char* name) {
    char* dst = reinterpret_cast<char*>(msg.data);
    copy_bounded_name(dst, INITD_SERVICE_NAME_MAX, name);
}

void decode_producer_name(const message& msg, char* name_out) {
    const char* src = reinterpret_cast<const char*>(&msg.data[1]);
    copy_bounded_name(name_out, TRACED_PRODUCER_NAME_MAX, src);
}

uint64_t pack_u32_pair(uint32_t low, uint32_t high) {
    return static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
}

int write_all(int fd, const void* buffer, size_t size) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t total = 0;
    while (total < size) {
        ssize_t written = write(fd, ptr + total, size - total);
        if (written <= 0) return -1;
        total += static_cast<size_t>(written);
    }
    return 0;
}

int read_all(int fd, void* buffer, size_t size) {
    char* ptr = static_cast<char*>(buffer);
    size_t total = 0;
    while (total < size) {
        ssize_t got = read(fd, ptr + total, size - total);
        if (got <= 0) return -1;
        total += static_cast<size_t>(got);
    }
    return 0;
}

void send_reply(uint32_t target, uint32_t type, uint64_t d0 = 0, uint64_t d1 = 0, uint64_t d2 = 0, uint64_t d3 = 0) {
    message reply = {};
    reply.type = type;
    reply.data[0] = d0;
    reply.data[1] = d1;
    reply.data[2] = d2;
    reply.data[3] = d3;
    syscall(SYS_IPC_SEND, target, reinterpret_cast<long>(&reply));
}

uint64_t current_session_meta() {
    return pack_u32_pair(g_session_generation, g_session_active ? 1u : 0u);
}

producer_entry* find_producer(const char* name, uint32_t category) {
    for (auto& producer : g_producers) {
        if (!producer.used) continue;
        if (producer.category == category && names_equal(producer.name, name)) return &producer;
    }
    return nullptr;
}

producer_entry* allocate_producer() {
    for (auto& producer : g_producers) {
        if (!producer.used) return &producer;
    }
    return nullptr;
}

uint32_t producer_count() {
    uint32_t count = 0;
    for (const auto& producer : g_producers) {
        if (producer.used) ++count;
    }
    return count;
}

void handle_session_control(uint32_t sender, uint32_t type) {
    int rc = 0;

    switch (type) {
    case TRACED_MSG_START_SESSION:
        if (!g_session_active) {
            rc = trace_reset();
            if (rc < 0) break;
            rc = trace_enable();
            if (rc < 0) break;
            g_session_active = true;
            ++g_session_generation;
            g_last_snapshot_bytes = 0;
        }
        break;
    case TRACED_MSG_STOP_SESSION:
        if (g_session_active) {
            rc = trace_disable();
            if (rc < 0) break;
            g_session_active = false;
        }
        break;
    case TRACED_MSG_RESET_SESSION:
        rc = trace_reset();
        if (rc < 0) break;
        g_last_snapshot_bytes = 0;
        break;
    default:
        rc = -1;
        break;
    }

    send_reply(sender, TRACED_MSG_CONTROL_REPLY, rc < 0 ? TRACED_CTL_EFAIL : TRACED_CTL_OK, current_session_meta(),
               producer_count());
}

void handle_info(uint32_t sender) {
    trace_stats stats = {};
    if (trace_get_stats(&stats) < 0) {
        send_reply(sender, TRACED_MSG_INFO_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    send_reply(sender, TRACED_MSG_INFO_REPLY, TRACED_CTL_OK, pack_u32_pair(stats.enabled, stats.clock_id),
               pack_u32_pair(stats.cpu_count, stats.record_capacity_per_cpu), stats.clock_freq_hz);
}

void handle_counters(uint32_t sender) {
    trace_stats stats = {};
    if (trace_get_stats(&stats) < 0) {
        send_reply(sender, TRACED_MSG_COUNTERS_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    send_reply(sender, TRACED_MSG_COUNTERS_REPLY, TRACED_CTL_OK, stats.records_written, stats.records_overwritten,
               stats.records_available);
}

void handle_get_session(uint32_t sender) {
    send_reply(sender, TRACED_MSG_SESSION_REPLY, TRACED_CTL_OK, current_session_meta(), producer_count(),
               g_last_snapshot_bytes);
}

void handle_register_producer(uint32_t sender, const message& msg) {
    char producer_name[TRACED_PRODUCER_NAME_MAX] = {};
    decode_producer_name(msg, producer_name);
    if (producer_name[0] == '\0') {
        send_reply(sender, TRACED_MSG_PRODUCER_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    uint32_t category = static_cast<uint32_t>(msg.data[0] & 0xffffffffULL);
    producer_entry* producer = find_producer(producer_name, category);
    if (!producer) {
        producer = allocate_producer();
        if (!producer) {
            send_reply(sender, TRACED_MSG_PRODUCER_REPLY, TRACED_CTL_EFAIL);
            return;
        }

        producer->used = true;
        producer->id = g_next_producer_id++;
        producer->category = category;
        copy_bounded_name(producer->name, sizeof(producer->name), producer_name);
    }

    producer->tid = sender;
    send_reply(sender, TRACED_MSG_PRODUCER_REPLY, TRACED_CTL_OK, producer->id, current_session_meta(),
               producer->category);
}

void handle_get_producer(uint32_t sender, const message& msg) {
    uint32_t index = static_cast<uint32_t>(msg.data[0]);
    uint32_t current = 0;
    producer_entry* producer = nullptr;

    for (auto& entry : g_producers) {
        if (!entry.used) continue;
        if (current == index) {
            producer = &entry;
            break;
        }
        ++current;
    }

    if (!producer) {
        send_reply(sender, TRACED_MSG_PRODUCER_INFO_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    message reply = {};
    reply.type = TRACED_MSG_PRODUCER_INFO_REPLY;
    reply.data[0] = pack_u32_pair(TRACED_CTL_OK, producer->id);
    reply.data[1] = pack_u32_pair(producer->category, producer->tid);
    copy_bounded_name(reinterpret_cast<char*>(&reply.data[2]), TRACED_PRODUCER_NAME_MAX, producer->name);
    syscall(SYS_IPC_SEND, sender, reinterpret_cast<long>(&reply));
}

void handle_capture_snapshot(uint32_t sender) {
    trace_stats stats = {};
    if (trace_get_stats(&stats) < 0) {
        send_reply(sender, TRACED_MSG_SNAPSHOT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    size_t snapshot_size = sizeof(trace_snapshot_header) + (stats.records_available + 32) * sizeof(trace_record);
    if (snapshot_size < (128 * 1024)) snapshot_size = 128 * 1024;
    if (snapshot_size > (2 * 1024 * 1024)) snapshot_size = 2 * 1024 * 1024;

    void* buffer = mmap(nullptr, snapshot_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        send_reply(sender, TRACED_MSG_SNAPSHOT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    ssize_t bytes = trace_snapshot(buffer, snapshot_size);
    if (bytes < 0) {
        munmap(buffer, snapshot_size);
        send_reply(sender, TRACED_MSG_SNAPSHOT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    int fd = open(TRACED_SNAPSHOT_PATH, O_WRONLY);
    if (fd < 0) {
        munmap(buffer, snapshot_size);
        send_reply(sender, TRACED_MSG_SNAPSHOT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    ssize_t written = write(fd, buffer, static_cast<size_t>(bytes));
    close(fd);
    munmap(buffer, snapshot_size);

    if (written != bytes) {
        send_reply(sender, TRACED_MSG_SNAPSHOT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    g_last_snapshot_bytes = static_cast<uint64_t>(bytes);
    send_reply(sender, TRACED_MSG_SNAPSHOT_REPLY, TRACED_CTL_OK, static_cast<uint64_t>(bytes), current_session_meta());
}

void handle_capture_export(uint32_t sender) {
    trace_stats stats = {};
    if (trace_get_stats(&stats) < 0) {
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    size_t snapshot_size = sizeof(trace_snapshot_header) + (stats.records_available + 32) * sizeof(trace_record);
    if (snapshot_size < (128 * 1024)) snapshot_size = 128 * 1024;
    if (snapshot_size > (2 * 1024 * 1024)) snapshot_size = 2 * 1024 * 1024;

    void* kernel_buffer = mmap(nullptr, snapshot_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (kernel_buffer == MAP_FAILED) {
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    ssize_t kernel_bytes = trace_snapshot(kernel_buffer, snapshot_size);
    if (kernel_bytes < 0) {
        munmap(kernel_buffer, snapshot_size);
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    int export_fd = open(TRACED_EXPORT_PATH, O_WRONLY);
    if (export_fd < 0) {
        munmap(kernel_buffer, snapshot_size);
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    traced_export_header export_header = {};
    export_header.magic = TRACED_EXPORT_MAGIC;
    export_header.version = TRACED_EXPORT_VERSION;
    export_header.header_size = sizeof(traced_export_header);
    export_header.section_count = 0;
    export_header.total_size = sizeof(traced_export_header);

    if (lseek(export_fd, 0, SEEK_SET) < 0 || write_all(export_fd, &export_header, sizeof(export_header)) < 0) {
        close(export_fd);
        munmap(kernel_buffer, snapshot_size);
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    traced_export_section_header kernel_section = {};
    kernel_section.type = TRACED_EXPORT_SECTION_KERNEL_SNAPSHOT;
    kernel_section.section_size = sizeof(traced_export_section_header) + static_cast<uint32_t>(kernel_bytes);
    kernel_section.clock_id = TRACED_EXPORT_CLOCK_KERNEL_RAW;

    if (write_all(export_fd, &kernel_section, sizeof(kernel_section)) < 0 ||
        write_all(export_fd, kernel_buffer, static_cast<size_t>(kernel_bytes)) < 0) {
        close(export_fd);
        munmap(kernel_buffer, snapshot_size);
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    export_header.section_count++;
    export_header.total_size += kernel_section.section_size;
    munmap(kernel_buffer, snapshot_size);

    constexpr size_t max_records =
        (TRACE_PRODUCER_BUFFER_BYTES - sizeof(trace_producer_buffer_header)) / sizeof(trace_producer_record);
    trace_producer_record ordered_records[max_records] = {};

    for (auto& producer : g_producers) {
        if (!producer.used || producer.id == 0 || producer.id > TRACE_PRODUCER_SLOT_COUNT) continue;

        char path[TRACE_PRODUCER_PATH_MAX] = {};
        int path_len = snprintf(path, sizeof(path), "%s%u", TRACE_PRODUCER_FILE_PREFIX, producer.id);
        if (path_len <= 0 || static_cast<size_t>(path_len) >= sizeof(path)) continue;

        int producer_fd = open(path, O_RDONLY);
        if (producer_fd < 0) continue;

        trace_producer_buffer_header producer_header = {};
        if (read_all(producer_fd, &producer_header, sizeof(producer_header)) < 0 ||
            producer_header.magic != TRACE_PRODUCER_MAGIC || producer_header.version != TRACE_PRODUCER_VERSION) {
            close(producer_fd);
            continue;
        }

        uint32_t available_records = 0;
        if (producer_header.wrapped) {
            available_records = static_cast<uint32_t>(max_records);
        } else if (producer_header.write_offset >= sizeof(trace_producer_buffer_header)) {
            available_records = static_cast<uint32_t>(
                (producer_header.write_offset - sizeof(trace_producer_buffer_header)) / sizeof(trace_producer_record));
        }
        if (available_records > producer_header.record_count) available_records = producer_header.record_count;
        if (available_records == 0) {
            close(producer_fd);
            continue;
        }

        uint32_t loaded = 0;
        if (!producer_header.wrapped) {
            if (lseek(producer_fd, static_cast<long>(sizeof(trace_producer_buffer_header)), SEEK_SET) < 0 ||
                read_all(producer_fd, ordered_records, available_records * sizeof(trace_producer_record)) < 0) {
                close(producer_fd);
                continue;
            }
            loaded = available_records;
        } else {
            uint32_t tail_records = static_cast<uint32_t>((TRACE_PRODUCER_BUFFER_BYTES - producer_header.write_offset) /
                                                          sizeof(trace_producer_record));
            if (tail_records > available_records) tail_records = available_records;

            if (tail_records != 0) {
                if (lseek(producer_fd, static_cast<long>(producer_header.write_offset), SEEK_SET) < 0 ||
                    read_all(producer_fd, ordered_records, tail_records * sizeof(trace_producer_record)) < 0) {
                    close(producer_fd);
                    continue;
                }
                loaded = tail_records;
            }

            uint32_t head_records = available_records - tail_records;
            if (head_records != 0) {
                if (lseek(producer_fd, static_cast<long>(sizeof(trace_producer_buffer_header)), SEEK_SET) < 0 ||
                    read_all(producer_fd, ordered_records + loaded, head_records * sizeof(trace_producer_record)) < 0) {
                    close(producer_fd);
                    continue;
                }
                loaded += head_records;
            }
        }

        close(producer_fd);
        if (loaded == 0) continue;

        traced_export_section_header producer_section = {};
        producer_section.type = TRACED_EXPORT_SECTION_PRODUCER_RECORDS;
        producer_section.section_size = sizeof(traced_export_section_header) + loaded * sizeof(trace_producer_record);
        producer_section.clock_id = TRACED_EXPORT_CLOCK_MONOTONIC_NS;
        producer_section.producer_id = producer.id;
        producer_section.category = producer.category;
        producer_section.tid = producer.tid;
        producer_section.record_count = loaded;
        copy_bounded_name(producer_section.name, sizeof(producer_section.name), producer.name);

        if (write_all(export_fd, &producer_section, sizeof(producer_section)) < 0 ||
            write_all(export_fd, ordered_records, loaded * sizeof(trace_producer_record)) < 0) {
            close(export_fd);
            send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
            return;
        }

        export_header.section_count++;
        export_header.total_size += producer_section.section_size;
    }

    if (lseek(export_fd, 0, SEEK_SET) < 0 || write_all(export_fd, &export_header, sizeof(export_header)) < 0) {
        close(export_fd);
        send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_EFAIL);
        return;
    }

    close(export_fd);
    send_reply(sender, TRACED_MSG_EXPORT_REPLY, TRACED_CTL_OK, export_header.total_size, current_session_meta());
}

void dispatch_traced_message(const message& msg) {
    switch (msg.type) {
    case TRACED_MSG_START_SESSION:
    case TRACED_MSG_STOP_SESSION:
    case TRACED_MSG_RESET_SESSION:
        handle_session_control(msg.sender, msg.type);
        break;
    case TRACED_MSG_GET_INFO:
        handle_info(msg.sender);
        break;
    case TRACED_MSG_GET_COUNTERS:
        handle_counters(msg.sender);
        break;
    case TRACED_MSG_GET_SESSION:
        handle_get_session(msg.sender);
        break;
    case TRACED_MSG_REGISTER_PRODUCER:
        handle_register_producer(msg.sender, msg);
        break;
    case TRACED_MSG_GET_PRODUCER:
        handle_get_producer(msg.sender, msg);
        break;
    case TRACED_MSG_CAPTURE_SNAPSHOT:
        handle_capture_snapshot(msg.sender);
        break;
    case TRACED_MSG_CAPTURE_EXPORT:
        handle_capture_export(msg.sender);
        break;
    default:
        printf("traced: unhandled message type %u from %u\n", msg.type, msg.sender);
        break;
    }
}

} // namespace

int main() {
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    copy_service_name(reg, "traced");
    syscall(SYS_IPC_SEND, 1, reinterpret_cast<long>(&reg));

    while (true) {
        message msg = {};
        syscall(SYS_IPC_RECV, reinterpret_cast<long>(&msg), 0, 0);

        if (msg.sender == 1 && msg.type == INITD_MSG_CONTROL_REPLY) {
            break;
        }

        dispatch_traced_message(msg);
    }

    printf("traced: online\n");

    while (true) {
        message msg = {};
        syscall(SYS_IPC_RECV, reinterpret_cast<long>(&msg), 0, 0);
        dispatch_traced_message(msg);
    }

    return 0;
}
