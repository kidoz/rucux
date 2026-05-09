// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

#include "../dmesg/journal_view.hpp"

namespace {

constexpr const char* DMESG_MARKER_PATH = "/run/log/dmesg_smoke.ok";
constexpr const char* UNIXSOCK_MARKER_PATH = "/run/log/unixsock_smoke.ok";
constexpr const char* REBOOT_MARKER_PATH = "/run/log/reboot_smoke.ok";
constexpr const char* PERSISTENT_JOURNAL_PATH = "/fat32/journal.log";
constexpr const char* PASS_MARKER = "PASS\n";
constexpr int RETRY_LIMIT = 50000;

bool marker_passed(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    char buffer[16] = {};
    ssize_t got = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (got < 0) {
        return false;
    }

    buffer[got] = '\0';
    return strcmp(buffer, PASS_MARKER) == 0;
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

#if !defined(__arm__)
unsigned long long detect_boot_id() {
    char* journal = nullptr;
    size_t journal_size = 0;
    if (journal_view::read_entire_file(PERSISTENT_JOURNAL_PATH, &journal, &journal_size) != 0) {
        return 0;
    }

    unsigned long long boot_id = journal_view::detect_current_boot_id(journal, journal_size);
    free(journal);
    return boot_id;
}
#endif

} // namespace

int main() {
    printf("reboot_smoke: waiting for smoke markers\n");

    bool ready = false;
    for (int attempt = 0; attempt < RETRY_LIMIT; ++attempt) {
#if defined(__arm__)
        if (marker_passed(UNIXSOCK_MARKER_PATH)) {
#else
        if (marker_passed(DMESG_MARKER_PATH) && marker_passed(UNIXSOCK_MARKER_PATH)) {
#endif
            ready = true;
            break;
        }
        sched_yield();
    }

    if (!ready) {
#if defined(__arm__)
        printf("reboot_smoke: unixsock marker not ready\n");
#else
        printf("reboot_smoke: prerequisite smoke markers not ready\n");
#endif
        return 1;
    }

#if defined(__arm__)
    if (!write_marker(REBOOT_MARKER_PATH)) {
        printf("reboot_smoke: failed to write completion marker\n");
        return 1;
    }

    printf("reboot_smoke: PASS armv7, requesting reboot\n");
    int rc = reboot(RB_AUTOBOOT);
    printf("reboot_smoke: reboot failed rc=%d\n", rc);
    return 1;
#else
    unsigned long long boot_id = 0;
    for (int attempt = 0; attempt < RETRY_LIMIT; ++attempt) {
        boot_id = detect_boot_id();
        if (boot_id != 0) {
            break;
        }
        sched_yield();
    }

    if (boot_id == 0) {
        printf("reboot_smoke: failed to detect persistent boot id\n");
        return 1;
    }

    if (!write_marker(REBOOT_MARKER_PATH)) {
        printf("reboot_smoke: failed to write completion marker\n");
        return 1;
    }

    if (boot_id == 1) {
        printf("reboot_smoke: PASS boot=1, requesting reboot\n");
        int rc = reboot(RB_AUTOBOOT);
        printf("reboot_smoke: reboot failed rc=%d\n", rc);
        return 1;
    }

    if (boot_id == 2) {
        printf("reboot_smoke: PASS boot=2, requesting poweroff\n");
        int rc = reboot(RB_POWER_OFF);
        printf("reboot_smoke: poweroff failed rc=%d\n", rc);
        return 1;
    }

    printf("reboot_smoke: unexpected boot id %llu\n", boot_id);
    return 1;
#endif
}
