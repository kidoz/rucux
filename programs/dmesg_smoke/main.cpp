// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../dmesg/journal_view.hpp"

namespace {

constexpr const char* JOURNAL_PATH = "/run/log/journal.log";
constexpr const char* PERSISTENT_JOURNAL_PATH = "/fat32/journal.log";
constexpr const char* SMOKE_MARKER_PATH = "/run/log/dmesg_smoke.ok";
constexpr const char* PASS_MARKER = "PASS\n";
constexpr int RETRY_LIMIT = 50000;

bool journal_ready(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    char buffer[4096];
    ssize_t got = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    return got > 0;
}

bool contains_expected_boot_banner(const char* text) {
    if (!text) {
        return false;
    }
#if defined(__arm__)
    if (strstr(text, "@rucux-journal boot=") != nullptr) {
        return true;
    }
#endif
    return strstr(text, "kernel: rucux (amd64) Initialized (UEFI)!") != nullptr ||
           strstr(text, "kernel: rucux (armv7) Initialized!") != nullptr;
}

bool verify_current_boot_view(bool persistent_available) {
    if (!persistent_available) {
        char* journal = nullptr;
        size_t journal_size = 0;
        if (journal_view::read_entire_file(JOURNAL_PATH, &journal, &journal_size) != 0) {
            return false;
        }
        bool ok = contains_expected_boot_banner(journal);
        free(journal);
        return ok;
    }

    char* section = nullptr;
    size_t section_size = 0;
    unsigned long long boot_id = 0;
    journal_view::boot_selector selector = {
        journal_view::boot_selector_kind::CURRENT,
        0,
    };

    if (journal_view::extract_boot_section(PERSISTENT_JOURNAL_PATH, selector,
                                           &section, &section_size, &boot_id) != 0) {
        return false;
    }

    bool ok = boot_id != 0 &&
              strstr(section, "@rucux-journal boot=") != nullptr &&
              contains_expected_boot_banner(section);

    free(section);
    return ok;
}

bool write_marker(const char* path) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }

    bool ok = write(fd, PASS_MARKER, strlen(PASS_MARKER)) == static_cast<ssize_t>(strlen(PASS_MARKER));
    close(fd);
    return ok;
}

} // namespace

int main() {
    printf("dmesg_smoke: starting\n");

    bool journal_is_ready = false;
    bool persistent_available = false;
    for (int attempt = 0; attempt < RETRY_LIMIT; ++attempt) {
        if (!journal_is_ready && journal_ready(JOURNAL_PATH)) {
            journal_is_ready = true;
        }
        if (!persistent_available && journal_ready(PERSISTENT_JOURNAL_PATH)) {
            persistent_available = true;
        }
        if (journal_is_ready) {
            break;
        }
        sched_yield();
    }

    if (!journal_is_ready) {
        printf("dmesg_smoke: volatile journal content not ready\n");
        return 1;
    }

    bool current_boot_ready = false;
    for (int attempt = 0; attempt < RETRY_LIMIT; ++attempt) {
        if (verify_current_boot_view(persistent_available)) {
            current_boot_ready = true;
            break;
        }
        sched_yield();
    }

    if (!current_boot_ready) {
        printf("dmesg_smoke: current-boot view invalid\n");
        return 1;
    }

    if (journal_view::dump_path_to_fd(JOURNAL_PATH, STDOUT_FILENO) != 0) {
        printf("dmesg_smoke: FAILED at journal dump\n");
        return 1;
    }

    if (!write_marker(SMOKE_MARKER_PATH)) {
        printf("dmesg_smoke: FAILED at marker write\n");
        return 1;
    }

    printf("dmesg_smoke: PASS\n");
    return 0;
}
