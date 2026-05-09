// SPDX-License-Identifier: MIT
#include <kernel/log.hpp>

#include <kernel/cpu/percpu.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/trace.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>
#include <knew.hpp>

namespace kernel::log {
namespace {

static constexpr size_t LOG_RING_CAPACITY = 128;
static constexpr int POLLIN_K = 0x001;
static constexpr int POLLOUT_K = 0x004;

struct log_record {
    uint64_t sequence;
    uint64_t timestamp;
    uint32_t cpu_id;
    uint32_t thread_id;
    uint32_t source;
    uint32_t priority;
    uint16_t ident_length;
    uint16_t message_length;
    char ident[RUCUX_LOG_IDENT_MAX];
    char message[RUCUX_LOG_TEXT_MAX];
};

static irq_spinlock g_log_lock;
static bool g_initialized = false;
static uint64_t g_records_written = 0;
static log_record g_records[LOG_RING_CAPACITY] = {};
static char g_console_line[RUCUX_LOG_TEXT_MAX] = {};
static size_t g_console_line_length = 0;

static kernel::vfs::vfs_node g_kmsg_node = {};

static uint64_t oldest_visible_sequence_locked() noexcept {
    if (g_records_written > LOG_RING_CAPACITY) {
        return g_records_written - LOG_RING_CAPACITY;
    }
    return 0;
}

static uint32_t current_cpu_id() noexcept {
    auto* pcpu = kernel::cpu::this_cpu();
    if (!pcpu || pcpu->cpu_id >= kernel::cpu::MAX_CPUS) {
        return 0xffffffffu;
    }
    return pcpu->cpu_id;
}

static uint32_t current_thread_id() noexcept {
    auto* thread = kernel::scheduler::scheduler::current_thread();
    return thread ? thread->tid : 0u;
}

static size_t copy_bounded(char* dst, size_t dst_size, const char* src, size_t src_len) noexcept {
    if (!dst || dst_size == 0) {
        return 0;
    }

    size_t len = src_len;
    if (!src) {
        len = 0;
    }
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    if (len != 0) {
        lib::memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return len;
}

static void append_record_locked(uint32_t source,
                                 uint32_t priority,
                                 const char* ident,
                                 size_t ident_length,
                                 const char* message,
                                 size_t message_length) noexcept {
    uint64_t sequence = g_records_written++;
    log_record& record = g_records[sequence % LOG_RING_CAPACITY];

    record.sequence = sequence;
    record.timestamp = kernel::trace::clock_now();
    record.cpu_id = current_cpu_id();
    record.thread_id = current_thread_id();
    record.source = source;
    record.priority = priority;
    record.ident_length = static_cast<uint16_t>(copy_bounded(record.ident, sizeof(record.ident), ident, ident_length));
    record.message_length = static_cast<uint16_t>(copy_bounded(record.message, sizeof(record.message), message, message_length));
}

static void append_record(uint32_t source,
                          uint32_t priority,
                          const char* ident,
                          size_t ident_length,
                          const char* message,
                          size_t message_length) noexcept {
    if (!g_initialized || !message) {
        return;
    }

    irq_lock_guard guard(g_log_lock);
    append_record_locked(source, priority, ident, ident_length, message, message_length);
}

static void flush_console_line_locked() noexcept {
    if (g_console_line_length == 0) {
        return;
    }

    append_record_locked(RUCUX_LOG_SOURCE_KERNEL,
                         RUCUX_LOG_INFO,
                         "kernel",
                         6,
                         g_console_line,
                         g_console_line_length);
    g_console_line_length = 0;
    g_console_line[0] = '\0';
}

static void append_char(char* buffer, size_t capacity, size_t& length, char ch) noexcept {
    if (length + 1 >= capacity) {
        return;
    }
    buffer[length++] = ch;
    buffer[length] = '\0';
}

static void append_string(char* buffer, size_t capacity, size_t& length, const char* text) noexcept {
    if (!text) {
        return;
    }
    while (*text != '\0' && length + 1 < capacity) {
        buffer[length++] = *text++;
    }
    buffer[length] = '\0';
}

static void append_u64(char* buffer, size_t capacity, size_t& length, uint64_t value) noexcept {
    char digits[32];
    size_t count = 0;
    if (value == 0) {
        append_char(buffer, capacity, length, '0');
        return;
    }

    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) {
        append_char(buffer, capacity, length, digits[--count]);
    }
}

static size_t format_record(const log_record& record, char* buffer, size_t capacity) noexcept {
    if (!buffer || capacity == 0) {
        return 0;
    }

    size_t length = 0;
    buffer[0] = '\0';

    append_u64(buffer, capacity, length, record.priority);
    append_char(buffer, capacity, length, ',');
    append_u64(buffer, capacity, length, record.sequence);
    append_char(buffer, capacity, length, ',');
    append_u64(buffer, capacity, length, record.timestamp);
    append_char(buffer, capacity, length, ',');
    append_u64(buffer, capacity, length, record.source);
    append_char(buffer, capacity, length, ',');
    append_u64(buffer, capacity, length, record.cpu_id);
    append_char(buffer, capacity, length, ',');
    append_u64(buffer, capacity, length, record.thread_id);
    append_char(buffer, capacity, length, ';');
    if (record.ident_length != 0) {
        append_string(buffer, capacity, length, record.ident);
        append_string(buffer, capacity, length, ": ");
    }
    append_string(buffer, capacity, length, record.message);
    append_char(buffer, capacity, length, '\n');
    return length;
}

static size_t copy_records_locked(uint64_t start_sequence,
                                  char* buffer,
                                  size_t buffer_size,
                                  uint64_t* next_sequence_out,
                                  uint64_t* dropped_records_out) noexcept {
    if (next_sequence_out) {
        *next_sequence_out = start_sequence;
    }
    if (dropped_records_out) {
        *dropped_records_out = 0;
    }
    if (!buffer || buffer_size == 0) {
        return 0;
    }

    uint64_t oldest = oldest_visible_sequence_locked();
    uint64_t sequence = start_sequence;
    if (sequence < oldest) {
        if (dropped_records_out) {
            *dropped_records_out = oldest - sequence;
        }
        sequence = oldest;
    }
    if (sequence > g_records_written) {
        sequence = g_records_written;
    }

    size_t copied = 0;
    while (sequence < g_records_written) {
        const log_record& record = g_records[sequence % LOG_RING_CAPACITY];
        char line[384];
        size_t line_length = format_record(record, line, sizeof(line));
        if (line_length == 0) {
            ++sequence;
            continue;
        }
        if (line_length > (buffer_size - copied)) {
            break;
        }
        lib::memcpy(buffer + copied, line, line_length);
        copied += line_length;
        ++sequence;
    }

    if (next_sequence_out) {
        *next_sequence_out = sequence;
    }
    return copied;
}

static size_t kmsg_read(kernel::vfs::vfs_node*, size_t offset, size_t size, void* buffer) noexcept {
    if (!buffer || size == 0) {
        return 0;
    }

    auto* out = static_cast<char*>(buffer);
    size_t copied = 0;
    size_t stream_offset = 0;

    irq_lock_guard guard(g_log_lock);

    for (uint64_t sequence = oldest_visible_sequence_locked(); sequence < g_records_written && copied < size; ++sequence) {
        const log_record& record = g_records[sequence % LOG_RING_CAPACITY];
        char line[384];
        size_t line_length = format_record(record, line, sizeof(line));
        if (line_length == 0) {
            continue;
        }
        if (offset >= stream_offset + line_length) {
            stream_offset += line_length;
            continue;
        }

        size_t line_offset = (offset > stream_offset) ? (offset - stream_offset) : 0;
        size_t available = line_length - line_offset;
        size_t remaining = size - copied;
        size_t chunk = available < remaining ? available : remaining;
        lib::memcpy(out + copied, line + line_offset, chunk);
        copied += chunk;
        stream_offset += line_length;
    }

    return copied;
}

static size_t kmsg_write(kernel::vfs::vfs_node*, size_t, size_t size, const void* buffer) noexcept {
    if (!buffer || size == 0) {
        return 0;
    }

    append_record(RUCUX_LOG_SOURCE_USER, RUCUX_LOG_INFO, "kmsg", 4, static_cast<const char*>(buffer), size);
    return size;
}

static int kmsg_poll(kernel::vfs::vfs_node*) noexcept {
    irq_lock_guard guard(g_log_lock);
    int events = POLLOUT_K;
    if (g_records_written != 0) {
        events |= POLLIN_K;
    }
    return events;
}

static kernel::vfs::vfs_ops g_kmsg_ops = {
    .read = kmsg_read,
    .write = kmsg_write,
    .truncate = nullptr,
    .open = nullptr,
    .close = nullptr,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .mmap = nullptr,
    .fsync = nullptr,
    .poll = kmsg_poll,
};

static void clear_locked() noexcept {
    g_records_written = 0;
    lib::memset(g_records, 0, sizeof(g_records));
    g_console_line_length = 0;
    g_console_line[0] = '\0';
}

} // namespace

void init() noexcept {
    irq_lock_guard guard(g_log_lock);
    clear_locked();
    g_kmsg_node.name = "kmsg";
    g_kmsg_node.name_hash = kernel::vfs::vfs_node::hash_name("kmsg");
    g_kmsg_node.inode = 0;
    g_kmsg_node.length = 0;
    g_kmsg_node.type = kernel::vfs::file_type::CHAR_DEVICE;
    g_kmsg_node.uid = 0;
    g_kmsg_node.gid = 0;
    g_kmsg_node.mask = 0;
    g_kmsg_node.flags = 0;
    g_kmsg_node.ops = &g_kmsg_ops;
    g_kmsg_node.ptr = nullptr;
    g_initialized = true;
}

void capture_console_char(char c) noexcept {
    if (!g_initialized || c == '\0' || c == '\r') {
        return;
    }

    irq_lock_guard guard(g_log_lock);

    if (c == '\n') {
        flush_console_line_locked();
        return;
    }

    if (g_console_line_length + 1 >= sizeof(g_console_line)) {
        flush_console_line_locked();
    }

    if (g_console_line_length + 1 < sizeof(g_console_line)) {
        g_console_line[g_console_line_length++] = c;
        g_console_line[g_console_line_length] = '\0';
    }
}

kernel::vfs::vfs_node* create_kmsg_node() noexcept {
    return &g_kmsg_node;
}

long sys_log_ctl(uint32_t op, void* arg0, size_t arg1, uintptr_t arg2) noexcept {
    (void)arg2;

    switch (op) {
    case RUCUX_LOG_CTL_SUBMIT: {
        if (!arg0 || arg1 < sizeof(rucux_log_submit_request)) {
            return -1;
        }
        const auto* request = static_cast<const rucux_log_submit_request*>(arg0);
        if (!request->message) {
            return -1;
        }
        append_record(RUCUX_LOG_SOURCE_USER,
                      request->priority,
                      request->ident,
                      request->ident_length,
                      request->message,
                      request->message_length);
        return 0;
    }
    case RUCUX_LOG_CTL_CLEAR: {
        irq_lock_guard guard(g_log_lock);
        clear_locked();
        return 0;
    }
    case RUCUX_LOG_CTL_STATS: {
        if (!arg0 || arg1 < sizeof(rucux_log_stats)) {
            return -1;
        }
        auto* stats = static_cast<rucux_log_stats*>(arg0);
        irq_lock_guard guard(g_log_lock);
        stats->version = RUCUX_LOG_VERSION;
        stats->record_capacity = LOG_RING_CAPACITY;
        stats->records_written = g_records_written;
        stats->records_visible = (g_records_written < LOG_RING_CAPACITY) ? g_records_written : LOG_RING_CAPACITY;
        stats->records_overwritten = (g_records_written > LOG_RING_CAPACITY) ? (g_records_written - LOG_RING_CAPACITY) : 0;
        return 0;
    }
    case RUCUX_LOG_CTL_READ: {
        if (!arg0 || arg1 < sizeof(rucux_log_read_request)) {
            return -1;
        }
        auto* request = static_cast<rucux_log_read_request*>(arg0);
        if (!request->buffer || request->buffer_size == 0) {
            request->next_sequence = request->start_sequence;
            request->dropped_records = 0;
            request->bytes_written = 0;
            return 0;
        }

        char* scratch = new char[request->buffer_size];
        if (!scratch) {
            return -1;
        }

        size_t copied = 0;
        {
            irq_lock_guard guard(g_log_lock);
            copied = copy_records_locked(request->start_sequence,
                                         scratch,
                                         request->buffer_size,
                                         &request->next_sequence,
                                         &request->dropped_records);
            request->bytes_written = static_cast<uint32_t>(copied);
            request->reserved = 0;
        }
        if (copied != 0) {
            lib::memcpy(request->buffer, scratch, copied);
        }
        delete[] scratch;
        return 0;
    }
    default:
        return -1;
    }
}

} // namespace kernel::log
