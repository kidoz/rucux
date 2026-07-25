// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/journal.h>
#include <uapi/kernel/log.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

namespace {

constexpr const char* VOLATILE_JOURNAL_PATH = "/run/log/journal.log";
constexpr const char* PERSISTENT_JOURNAL_PATH = "/fat32/journal.log";
constexpr useconds_t POLL_INTERVAL_USEC = 200000;
constexpr size_t READ_BUFFER_SIZE = 8 * 1024;
constexpr size_t SEED_PROBE_SIZE = 512;
constexpr const char* ROTATION_NOTICE = "logd: persistent journal rotated after reaching preallocated capacity\n";
constexpr size_t BOOT_MARKER_BUFFER_SIZE = 160;

void logd_pause() {
    usleep(POLL_INTERVAL_USEC);
    sched_yield();
}

void copy_service_name(message& msg, const char* name) {
    char* dst = reinterpret_cast<char*>(msg.data);
    for (int i = 0; i < INITD_SERVICE_NAME_MAX; ++i) {
        dst[i] = name[i];
        if (name[i] == '\0') {
            break;
        }
    }
}

bool register_service(const char* name) {
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    copy_service_name(reg, name);
    syscall(SYS_IPC_SEND, 1, reinterpret_cast<long>(&reg));

    message ack = {};
    syscall(SYS_IPC_RECV, reinterpret_cast<long>(&ack), 0, 0);
    return ack.type == INITD_MSG_CONTROL_REPLY && ack.data[0] == INITD_CTL_OK;
}

int write_all(int fd, const void* buffer, size_t size) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t total = 0;
    while (total < size) {
        ssize_t written = write(fd, ptr + total, size - total);
        if (written <= 0) {
            return -1;
        }
        total += static_cast<size_t>(written);
    }
    return 0;
}

bool append_journal(int fd, const char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return true;
    }
    if (lseek(fd, 0, SEEK_END) < 0) {
        return false;
    }
    return write_all(fd, buffer, size) == 0;
}

bool rewrite_journal(int fd, const char* buffer, size_t size) {
    if (ftruncate(fd, 0) < 0) {
        return false;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return false;
    }
    return size == 0 ? true : write_all(fd, buffer, size) == 0;
}

bool seed_file_is_zero_filled(int fd) {
    char probe[SEED_PROBE_SIZE];
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return false;
    }

    ssize_t got = read(fd, probe, sizeof(probe));
    if (got <= 0) {
        return false;
    }

    for (ssize_t i = 0; i < got; ++i) {
        if (probe[i] != '\0') {
            return false;
        }
    }
    return true;
}

bool reset_journal(int fd) {
    return rewrite_journal(fd, nullptr, 0);
}

bool flush_journal(int fd) {
    return fsync(fd) == 0;
}

bool append_rotation_notice(int fd, uint64_t boot_id) {
    char notice[BOOT_MARKER_BUFFER_SIZE];
    int written = snprintf(notice, sizeof(notice), "%s%llu%s reason=capacity\n", RUCUX_JOURNAL_MARKER_PREFIX,
                           static_cast<unsigned long long>(boot_id), RUCUX_JOURNAL_EVENT_ROTATE);
    if (written <= 0) {
        return false;
    }
    size_t notice_size = static_cast<size_t>(written);
    if (notice_size >= sizeof(notice)) {
        notice_size = sizeof(notice) - 1;
    }
    return write_all(fd, notice, notice_size) == 0;
}

bool format_boot_marker(char* buffer, size_t capacity, uint64_t boot_id, uint64_t first_sequence, size_t* bytes_out) {
    if (!buffer || capacity == 0 || !bytes_out) {
        return false;
    }

    int written = snprintf(buffer, capacity, "%s%llu%s first_sequence=%llu\n", RUCUX_JOURNAL_MARKER_PREFIX,
                           static_cast<unsigned long long>(boot_id), RUCUX_JOURNAL_EVENT_START,
                           static_cast<unsigned long long>(first_sequence));
    if (written <= 0) {
        return false;
    }

    size_t size = static_cast<size_t>(written);
    if (size >= capacity) {
        size = capacity - 1;
    }
    *bytes_out = size;
    return true;
}

uint64_t parse_boot_marker(const char* line, size_t line_size) {
    if (!line || line_size == 0) {
        return 0;
    }

    const char* prefix = RUCUX_JOURNAL_MARKER_PREFIX;
    size_t prefix_len = strlen(prefix);
    if (line_size < prefix_len || strncmp(line, prefix, prefix_len) != 0) {
        return 0;
    }

    uint64_t boot_id = 0;
    for (size_t i = prefix_len; i < line_size; ++i) {
        char ch = line[i];
        if (ch < '0' || ch > '9') {
            break;
        }
        boot_id = (boot_id * 10) + static_cast<uint64_t>(ch - '0');
    }
    return boot_id;
}

uint64_t detect_next_boot_id(int fd) {
    if (fd < 0) {
        return 1;
    }

    long end = lseek(fd, 0, SEEK_END);
    if (end <= 0) {
        return 1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        return 1;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(end)));
    if (!buffer) {
        return 1;
    }

    size_t total = 0;
    while (total < static_cast<size_t>(end)) {
        ssize_t got = read(fd, buffer + total, static_cast<size_t>(end) - total);
        if (got <= 0) {
            free(buffer);
            return 1;
        }
        total += static_cast<size_t>(got);
    }

    uint64_t last_boot_id = 0;
    size_t line_start = 0;
    for (size_t i = 0; i <= total; ++i) {
        if (i == total || buffer[i] == '\n') {
            uint64_t boot_id = parse_boot_marker(buffer + line_start, i - line_start);
            if (boot_id > last_boot_id) {
                last_boot_id = boot_id;
            }
            line_start = i + 1;
        }
    }

    free(buffer);
    return last_boot_id + 1;
}

bool append_persistent_journal(int fd, const char* buffer, size_t size, uint64_t boot_id, const char* boot_marker,
                               size_t boot_marker_size) {
    if (append_journal(fd, buffer, size)) {
        return true;
    }

    if (!reset_journal(fd)) {
        return false;
    }
    if (write_all(fd, ROTATION_NOTICE, strlen(ROTATION_NOTICE)) != 0) {
        return false;
    }
    if (!append_rotation_notice(fd, boot_id)) {
        return false;
    }
    if (boot_marker_size != 0 && write_all(fd, boot_marker, boot_marker_size) != 0) {
        return false;
    }
    if (buffer == boot_marker && size == boot_marker_size) {
        return true;
    }
    return size == 0 ? true : write_all(fd, buffer, size) == 0;
}

bool fetch_log_stats(rucux_log_stats* stats) {
    if (!stats) {
        return false;
    }
    memset(stats, 0, sizeof(*stats));
    long rc = syscall(SYS_LOG_CTL, RUCUX_LOG_CTL_STATS, reinterpret_cast<long>(stats), sizeof(*stats));
    return rc == 0;
}

bool read_log_incremental(uint64_t start_sequence, char* buffer, size_t capacity, uint64_t* next_sequence_out,
                          uint64_t* dropped_records_out, size_t* bytes_out) {
    if (!buffer || capacity == 0 || !next_sequence_out || !dropped_records_out || !bytes_out) {
        return false;
    }

    rucux_log_read_request request = {};
    request.start_sequence = start_sequence;
    request.buffer = buffer;
    request.buffer_size = static_cast<uint32_t>(capacity);

    long rc = syscall(SYS_LOG_CTL, RUCUX_LOG_CTL_READ, reinterpret_cast<long>(&request), sizeof(request));
    if (rc != 0) {
        return false;
    }

    *next_sequence_out = request.next_sequence;
    *dropped_records_out = request.dropped_records;
    *bytes_out = request.bytes_written;
    return rc == 0;
}

void format_drop_notice(char* buffer, size_t capacity, uint64_t dropped_records, size_t* bytes_out) {
    if (!buffer || capacity == 0 || !bytes_out) {
        return;
    }
    int written = snprintf(buffer, capacity, "logd: dropped %llu records while catching up\n",
                           static_cast<unsigned long long>(dropped_records));
    if (written < 0) {
        *bytes_out = 0;
        return;
    }
    size_t size = static_cast<size_t>(written);
    if (size >= capacity) {
        size = capacity - 1;
    }
    *bytes_out = size;
}

} // namespace

int main() {
    if (!register_service("logd")) {
        printf("logd: failed to register with init\n");
        return 1;
    }

    int volatile_fd = open(VOLATILE_JOURNAL_PATH, O_WRONLY);
    if (volatile_fd < 0) {
        printf("logd: failed to open %s\n", VOLATILE_JOURNAL_PATH);
        return 1;
    }

    int persistent_fd = open(PERSISTENT_JOURNAL_PATH, O_RDWR);
    if (persistent_fd >= 0 && seed_file_is_zero_filled(persistent_fd) && !reset_journal(persistent_fd)) {
        close(persistent_fd);
        persistent_fd = -1;
    }

    static char buffer[READ_BUFFER_SIZE];
    static char notice_buffer[128];
    static char boot_marker[BOOT_MARKER_BUFFER_SIZE];
    uint64_t next_sequence = 0;
    uint64_t boot_id = 1;
    size_t boot_marker_size = 0;
    bool announced = false;
    bool initialized = false;

    while (true) {
        rucux_log_stats stats = {};
        if (fetch_log_stats(&stats)) {
            if (!initialized) {
                uint64_t oldest_visible = 0;
                if (stats.records_written > stats.records_visible) {
                    oldest_visible = stats.records_written - stats.records_visible;
                }
                next_sequence = oldest_visible;
                boot_id = detect_next_boot_id(persistent_fd);
                if (!format_boot_marker(boot_marker, sizeof(boot_marker), boot_id, next_sequence, &boot_marker_size)) {
                    logd_pause();
                    continue;
                }
                if (!rewrite_journal(volatile_fd, nullptr, 0)) {
                    logd_pause();
                    continue;
                }
                if (boot_marker_size != 0 && !append_journal(volatile_fd, boot_marker, boot_marker_size)) {
                    logd_pause();
                    continue;
                }
                if (!flush_journal(volatile_fd)) {
                    logd_pause();
                    continue;
                }
                if (persistent_fd >= 0 && boot_marker_size != 0 &&
                    !append_persistent_journal(persistent_fd, boot_marker, boot_marker_size, boot_id, boot_marker,
                                               boot_marker_size)) {
                    close(persistent_fd);
                    persistent_fd = -1;
                } else if (persistent_fd >= 0 && !flush_journal(persistent_fd)) {
                    close(persistent_fd);
                    persistent_fd = -1;
                }
                initialized = true;
            }

#if defined(__arm__)
            // ARMv7 uses a reduced journal path for now. The full incremental
            // replay path still wedges in the log-read syscall under QEMU, so
            // keep boot markers and liveness validation working until that
            // kernel-side replay bug is fixed.
            if (!announced) {
                if (persistent_fd >= 0) {
                    printf("logd: online, mirroring boot markers into %s and %s\n", VOLATILE_JOURNAL_PATH,
                           PERSISTENT_JOURNAL_PATH);
                } else {
                    printf("logd: online, mirroring boot markers into %s\n", VOLATILE_JOURNAL_PATH);
                }
                announced = true;
            }
            logd_pause();
            continue;
#endif

            while (next_sequence < stats.records_written) {
                uint64_t advanced_sequence = next_sequence;
                uint64_t dropped_records = 0;
                size_t bytes = 0;
                bool wrote_volatile = false;
                bool wrote_persistent = false;
                if (!read_log_incremental(next_sequence, buffer, sizeof(buffer), &advanced_sequence, &dropped_records,
                                          &bytes)) {
                    break;
                }

                if (dropped_records != 0) {
                    size_t notice_bytes = 0;
                    format_drop_notice(notice_buffer, sizeof(notice_buffer), dropped_records, &notice_bytes);
                    if (notice_bytes != 0 && !append_journal(volatile_fd, notice_buffer, notice_bytes)) {
                        break;
                    }
                    wrote_volatile = wrote_volatile || notice_bytes != 0;
                    if (persistent_fd >= 0 && notice_bytes != 0) {
                        if (!append_persistent_journal(persistent_fd, notice_buffer, notice_bytes, boot_id, boot_marker,
                                                       boot_marker_size)) {
                            close(persistent_fd);
                            persistent_fd = -1;
                        } else {
                            wrote_persistent = true;
                        }
                    }
                }

                if (bytes != 0 && !append_journal(volatile_fd, buffer, bytes)) {
                    break;
                }
                wrote_volatile = wrote_volatile || bytes != 0;
                if (persistent_fd >= 0 && bytes != 0) {
                    if (!append_persistent_journal(persistent_fd, buffer, bytes, boot_id, boot_marker,
                                                   boot_marker_size)) {
                        close(persistent_fd);
                        persistent_fd = -1;
                    } else {
                        wrote_persistent = true;
                    }
                }

                if (wrote_volatile && !flush_journal(volatile_fd)) {
                    break;
                }
                if (persistent_fd >= 0 && wrote_persistent && !flush_journal(persistent_fd)) {
                    close(persistent_fd);
                    persistent_fd = -1;
                }

                if (advanced_sequence == next_sequence && bytes == 0 && dropped_records == 0) {
                    break;
                }
                next_sequence = advanced_sequence;
            }

            if (!announced) {
                if (persistent_fd >= 0) {
                    printf("logd: online, mirroring incremental records into %s and %s\n", VOLATILE_JOURNAL_PATH,
                           PERSISTENT_JOURNAL_PATH);
                } else {
                    printf("logd: online, mirroring incremental records into %s\n", VOLATILE_JOURNAL_PATH);
                }
                announced = true;
            }
        }

        logd_pause();
    }
}
