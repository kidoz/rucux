// SPDX-License-Identifier: MIT
#pragma once

#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <uapi/kernel/journal.h>
#include <unistd.h>

namespace journal_view {

enum class boot_selector_kind {
    NONE,
    CURRENT,
    EXACT,
};

struct boot_selector {
    boot_selector_kind kind;
    unsigned long long boot_id;
};

inline int write_all_fd(int fd, const char* buffer, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t written = write(fd, buffer + total, size - total);
        if (written <= 0) {
            return -1;
        }
        total += static_cast<size_t>(written);
    }
    return 0;
}

inline int dump_path_to_fd(const char* path, int out_fd, size_t buffer_size = 1024) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    char* buffer = static_cast<char*>(malloc(buffer_size));
    if (!buffer) {
        close(fd);
        return -1;
    }

    while (true) {
        ssize_t got = read(fd, buffer, buffer_size);
        if (got < 0) {
            free(buffer);
            close(fd);
            return -1;
        }
        if (got == 0) {
            break;
        }
        if (write_all_fd(out_fd, buffer, static_cast<size_t>(got)) != 0) {
            free(buffer);
            close(fd);
            return -1;
        }
    }

    free(buffer);
    close(fd);
    return 0;
}

inline int read_entire_file(const char* path, char** buffer_out, size_t* size_out) {
    if (!buffer_out || !size_out) {
        return -1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    long end = lseek(fd, 0, SEEK_END);
    if (end < 0) {
        close(fd);
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    char* buffer = static_cast<char*>(malloc(static_cast<size_t>(end) + 1));
    if (!buffer) {
        close(fd);
        return -1;
    }

    size_t total = 0;
    while (total < static_cast<size_t>(end)) {
        ssize_t got = read(fd, buffer + total, static_cast<size_t>(end) - total);
        if (got <= 0) {
            free(buffer);
            close(fd);
            return -1;
        }
        total += static_cast<size_t>(got);
    }
    buffer[total] = '\0';

    close(fd);
    *buffer_out = buffer;
    *size_out = total;
    return 0;
}

inline unsigned long long parse_boot_marker(const char* line, size_t line_size) {
    const char* prefix = RUCUX_JOURNAL_MARKER_PREFIX;
    size_t prefix_len = strlen(prefix);
    if (line_size < prefix_len || strncmp(line, prefix, prefix_len) != 0) {
        return 0;
    }

    unsigned long long boot_id = 0;
    for (size_t i = prefix_len; i < line_size; ++i) {
        char ch = line[i];
        if (ch < '0' || ch > '9') {
            break;
        }
        boot_id = (boot_id * 10ULL) + static_cast<unsigned long long>(ch - '0');
    }
    return boot_id;
}

inline unsigned long long detect_current_boot_id(const char* buffer, size_t size) {
    unsigned long long current = 0;
    size_t line_start = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || buffer[i] == '\n') {
            unsigned long long boot_id = parse_boot_marker(buffer + line_start, i - line_start);
            if (boot_id > current) {
                current = boot_id;
            }
            line_start = i + 1;
        }
    }
    return current;
}

inline int extract_boot_section(const char* path,
                                boot_selector selector,
                                char** section_out,
                                size_t* section_size_out,
                                unsigned long long* boot_id_out = nullptr) {
    char* buffer = nullptr;
    size_t size = 0;
    if (read_entire_file(path, &buffer, &size) != 0) {
        return -1;
    }

    unsigned long long target_boot = selector.boot_id;
    if (selector.kind == boot_selector_kind::CURRENT) {
        target_boot = detect_current_boot_id(buffer, size);
    }
    if (boot_id_out) {
        *boot_id_out = target_boot;
    }

    if (selector.kind == boot_selector_kind::CURRENT && target_boot == 0) {
        *section_out = buffer;
        *section_size_out = size;
        return 0;
    }

    size_t section_start = size;
    size_t section_end = size;
    bool in_target = false;
    size_t line_start = 0;

    for (size_t i = 0; i <= size; ++i) {
        if (i == size || buffer[i] == '\n') {
            unsigned long long boot_id = parse_boot_marker(buffer + line_start, i - line_start);
            if (boot_id != 0) {
                if (in_target) {
                    section_end = line_start;
                    break;
                }
                if (boot_id == target_boot) {
                    section_start = line_start;
                    in_target = true;
                }
            }
            line_start = i + 1;
        }
    }

    if (!in_target) {
        free(buffer);
        return -1;
    }

    size_t section_size = section_end - section_start;
    char* section = static_cast<char*>(malloc(section_size + 1));
    if (!section) {
        free(buffer);
        return -1;
    }
    if (section_size != 0) {
        memcpy(section, buffer + section_start, section_size);
    }
    section[section_size] = '\0';
    free(buffer);

    *section_out = section;
    *section_size_out = section_size;
    return 0;
}

inline int dump_boot_section_to_fd(const char* path, boot_selector selector, int out_fd) {
    char* section = nullptr;
    size_t section_size = 0;
    if (extract_boot_section(path, selector, &section, &section_size) != 0) {
        return -1;
    }

    int rc = write_all_fd(out_fd, section, section_size);
    free(section);
    return rc;
}

} // namespace journal_view
