// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "journal_view.hpp"

namespace {

constexpr const char* VOLATILE_JOURNAL_PATH = "/run/log/journal.log";
constexpr const char* PERSISTENT_JOURNAL_PATH = "/fat32/journal.log";

} // namespace

int main(int argc, char** argv) {
    const char* requested = VOLATILE_JOURNAL_PATH;
    bool explicit_path = false;
    journal_view::boot_selector selector = {journal_view::boot_selector_kind::NONE, 0};

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--persistent") == 0) {
            requested = PERSISTENT_JOURNAL_PATH;
            continue;
        }
        if (strcmp(argv[i], "--boot") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "dmesg: missing value for --boot\n");
                return 1;
            }
            const char* value = argv[++i];
            if (strcmp(value, "current") == 0) {
                selector.kind = journal_view::boot_selector_kind::CURRENT;
                continue;
            }

            char* end = nullptr;
            unsigned long long boot_id = strtoull(value, &end, 10);
            if (!end || *end != '\0' || boot_id == 0) {
                fprintf(stderr, "dmesg: invalid boot selector '%s'\n", value);
                return 1;
            }
            selector.kind = journal_view::boot_selector_kind::EXACT;
            selector.boot_id = boot_id;
            continue;
        }

        if (argv[i][0] == '-') {
            fprintf(stderr, "dmesg: unknown option '%s'\n", argv[i]);
            return 1;
        }

        requested = argv[i];
        explicit_path = true;
    }

    if (selector.kind != journal_view::boot_selector_kind::NONE &&
        !explicit_path && strcmp(requested, VOLATILE_JOURNAL_PATH) == 0) {
        requested = PERSISTENT_JOURNAL_PATH;
    }

    if (selector.kind == journal_view::boot_selector_kind::NONE) {
        if (journal_view::dump_path_to_fd(requested, STDOUT_FILENO) == 0) {
            return 0;
        }
    } else if (journal_view::dump_boot_section_to_fd(requested, selector, STDOUT_FILENO) == 0) {
        return 0;
    }

    if (selector.kind == journal_view::boot_selector_kind::NONE && strcmp(requested, VOLATILE_JOURNAL_PATH) == 0) {
        if (journal_view::dump_path_to_fd("/dev/kmsg", STDOUT_FILENO) == 0) {
            return 0;
        }
    }

    fprintf(stderr, "dmesg: failed to read %s\n", requested);
    return 1;
}
