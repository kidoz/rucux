// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr const char* SHRINK = "/fat32/shrink.bin";
constexpr const char* REWRITE = "/fat32/rewrite.bin";
constexpr const char* STAGE = "/fat32/stage.bin";
constexpr size_t PAYLOAD_SIZE = 700;
uint8_t payload[PAYLOAD_SIZE];

bool verify_file(const char* path, size_t size, bool seed) {
    if (size > 2048 || (!seed && size > PAYLOAD_SIZE)) return false;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat info{};
    bool ok = fstat(fd, &info) == 0 && info.st_size == static_cast<int64_t>(size);
    ok = ok && stat(path, &info) == 0 && info.st_size == static_cast<int64_t>(size);
    uint8_t data[2048];
    if (ok && size) ok = read(fd, data, size) == static_cast<ssize_t>(size);
    for (size_t i = 0; ok && i < size; ++i) {
        uint8_t expected = seed ? static_cast<uint8_t>(i * 13 + 17) : payload[i];
        ok = data[i] == expected;
    }
    uint8_t extra;
    if (ok) ok = read(fd, &extra, 1) == 0;
    close(fd);
    return ok;
}

bool verify_ramfs() {
    int fd = open("/run/log/journal.log", O_RDWR);
    if (fd < 0) return false;
    bool ok = write(fd, "old-data", 8) == 8 && ftruncate(fd, 0) == 0 && fsync(fd) == 0;
    char bytes[8]{};
    ok = ok && read(fd, bytes, 1) == 0 && lseek(fd, 5, SEEK_SET) == 5 && write(fd, "x", 1) == 1 &&
         lseek(fd, 0, SEEK_SET) == 0 && read(fd, bytes, sizeof(bytes)) == 6;
    for (unsigned i = 0; ok && i < 5; ++i)
        ok = bytes[i] == 0;
    ok = ok && bytes[5] == 'x';
    close(fd);
    return ok;
}

bool check_errors() {
    errno = 0;
    if (fsync(-1) != -1 || errno != EBADF) return false;
    struct stat info{};
    errno = 0;
    if (fstat(-1, &info) != -1 || errno != EBADF) return false;
    errno = 0;
    if (stat("/fat32/missing.bin", &info) != -1 || errno != ENOENT) return false;
    errno = 0;
    if (lseek(-1, 0, SEEK_END) != -1 || errno != EBADF) return false;
    int fd = open(SHRINK, O_RDONLY);
    if (fd < 0) return false;
    errno = 0;
    bool ok = ftruncate(fd, 0) == -1 && errno == EINVAL;
    close(fd);
    fd = open(SHRINK, O_RDWR);
    if (fd < 0) return false;
    errno = 0;
    ok = ok && ftruncate(fd, -1) == -1 && errno == EINVAL;
    errno = 0;
    ok = ok && ftruncate(fd, 2048) == -1 && errno == EOPNOTSUPP;
    close(fd);
    return ok;
}
} // namespace

#define REQUIRE(condition)                                                                                             \
    do {                                                                                                               \
        const bool passed = (condition);                                                                               \
        if (!passed) {                                                                                                 \
            printf("storage_smoke: FAIL %s errno=%d\n", #condition, errno);                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main() {
    for (size_t i = 0; i < PAYLOAD_SIZE; ++i)
        payload[i] = static_cast<uint8_t>(i * 29 + 101);
    int stage_fd = open(STAGE, O_RDWR);
    REQUIRE(stage_fd >= 0);
    char stage = 0;
    REQUIRE(read(stage_fd, &stage, 1) == 1 && stage >= '0' && stage <= '2');
    REQUIRE(verify_ramfs());
    if (stage == '0') {
        REQUIRE(verify_file(SHRINK, 1024, true));
        int fd = open(SHRINK, O_RDWR);
        REQUIRE(fd >= 0);
        REQUIRE(lseek(fd, 100, SEEK_SET) == 100);
        REQUIRE(ftruncate(fd, 37) == 0 && fsync(fd) == 0);
        REQUIRE(lseek(fd, 0, SEEK_CUR) == 100);
        close(fd); // No later write is allowed to make this shrink durable.
        REQUIRE(verify_file(SHRINK, 37, true));
        fd = open(REWRITE, O_RDWR);
        REQUIRE(fd >= 0);
        const int observer = open(REWRITE, O_RDWR);
        const int seeker = open(REWRITE, O_RDWR);
        REQUIRE(observer >= 0 && seeker >= 0);
        REQUIRE(ftruncate(fd, 37) == 0);
        // Each old open must refresh independently, before any read/write/fsync.
        REQUIRE(lseek(seeker, 0, SEEK_END) == 37 && write(seeker, "x", 1) == 1);
        struct stat info{};
        REQUIRE(fstat(observer, &info) == 0 && info.st_size == 38);
        REQUIRE(stat(REWRITE, &info) == 0 && info.st_size == 38);
        REQUIRE(ftruncate(fd, 0) == 0 && fsync(fd) == 0);
        REQUIRE(lseek(observer, 0, SEEK_END) == 0);
        REQUIRE(verify_file(REWRITE, 0, false));
        REQUIRE(write(fd, payload, PAYLOAD_SIZE) == static_cast<ssize_t>(PAYLOAD_SIZE) && fsync(fd) == 0);
        REQUIRE(fstat(seeker, &info) == 0 && info.st_size == static_cast<int64_t>(PAYLOAD_SIZE));
        close(observer);
        close(seeker);
        close(fd);
        REQUIRE(verify_file(REWRITE, PAYLOAD_SIZE, false));
        REQUIRE(check_errors());
    } else if (stage == '1') {
        REQUIRE(verify_file(SHRINK, 37, true));
        REQUIRE(verify_file(REWRITE, PAYLOAD_SIZE, false));
        int fd = open(REWRITE, O_RDWR);
        REQUIRE(fd >= 0 && ftruncate(fd, 0) == 0 && fsync(fd) == 0);
        close(fd); // A second remount must see EOF zero without a later write.
    } else {
        REQUIRE(verify_file(SHRINK, 37, true));
        REQUIRE(verify_file(REWRITE, 0, false));
        int fd = open(REWRITE, O_RDWR);
        REQUIRE(fd >= 0);
        REQUIRE(write(fd, payload, PAYLOAD_SIZE) == static_cast<ssize_t>(PAYLOAD_SIZE) && fsync(fd) == 0);
        close(fd); // Rewriting after a zero-length remount requires retained capacity.
        REQUIRE(verify_file(REWRITE, PAYLOAD_SIZE, false));
    }
    ++stage;
    REQUIRE(lseek(stage_fd, 0, SEEK_SET) == 0 && write(stage_fd, &stage, 1) == 1 && fsync(stage_fd) == 0);
    close(stage_fd);
    printf("storage_smoke: PASS stage=%c\n", stage);
    reboot(RB_POWER_OFF);
    printf("storage_smoke: FAIL poweroff returned\n");
    return 1;
}
