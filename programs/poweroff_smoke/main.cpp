// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

namespace {

constexpr const char* DMESG_MARKER_PATH = "/run/log/dmesg_smoke.ok";
constexpr const char* UNIXSOCK_MARKER_PATH = "/run/log/unixsock_smoke.ok";
constexpr const char* POWEROFF_MARKER_PATH = "/run/log/poweroff_smoke.ok";
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

} // namespace

int main() {
    printf("poweroff_smoke: waiting for smoke markers\n");

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
        printf("poweroff_smoke: unixsock marker not ready\n");
#else
        printf("poweroff_smoke: prerequisite smoke markers not ready\n");
#endif
        return 1;
    }

    if (!write_marker(POWEROFF_MARKER_PATH)) {
        printf("poweroff_smoke: failed to write completion marker\n");
        return 1;
    }

    printf("poweroff_smoke: PASS, requesting poweroff\n");
    int rc = reboot(RB_POWER_OFF);
    printf("poweroff_smoke: poweroff failed rc=%d\n", rc);
    return 1;
}
