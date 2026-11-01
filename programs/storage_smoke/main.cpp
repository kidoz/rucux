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
constexpr const char* APPEND = "/fat32/append.bin";
constexpr const char* APPEND_FIRST = "first\nsecond\nthird\n";
constexpr const char* APPEND_SECOND = "first\nsecond\nthird\nboot2\n";
constexpr const char* APPEND_THIRD = "first\nsecond\nthird\nboot2\nboot3\n";
constexpr int READ_WRITE_APPEND = static_cast<int>(static_cast<unsigned>(O_RDWR) | static_cast<unsigned>(O_APPEND));
constexpr int WRITE_APPEND = static_cast<int>(static_cast<unsigned>(O_WRONLY) | static_cast<unsigned>(O_APPEND));
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
    ok = ok && fcntl(fd, F_SETFL, O_APPEND) == 0 && fcntl(fd, F_GETFL) == READ_WRITE_APPEND;
    ok = ok && lseek(fd, 0, SEEK_SET) == 0 && write(fd, "A", 1) == 1 && lseek(fd, 0, SEEK_CUR) == 7;
    ok = ok && fcntl(fd, F_SETFL, 0) == 0 && lseek(fd, 0, SEEK_SET) == 0 && write(fd, "B", 1) == 1;
    ok = ok && lseek(fd, 0, SEEK_SET) == 0 && read(fd, bytes, sizeof(bytes)) == 7;
    ok = ok && bytes[0] == 'B' && bytes[5] == 'x' && bytes[6] == 'A';
    close(fd);
    return ok;
}

bool check_errors() {
    errno = 0;
    if (fsync(-1) != -1 || errno != EBADF) return false;
    char byte = 'x';
    errno = 0;
    if (read(-1, &byte, 1) != -1 || errno != EBADF) return false;
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
    ok = ok && fcntl(fd, F_SETFL, READ_WRITE_APPEND) == 0 && fcntl(fd, F_GETFL) == O_APPEND;
    errno = 0;
    ok = ok && write(fd, "x", 1) == -1 && errno == EBADF;
    ok = ok && lseek(fd, 0, SEEK_END) == 37;
    errno = 123;
    ok = ok && read(fd, &byte, 1) == 0 && errno == 123;
    close(fd);
    fd = open(SHRINK, O_WRONLY);
    if (fd < 0) return false;
    errno = 0;
    ok = ok && read(fd, &byte, 1) == -1 && errno == EBADF && lseek(fd, 0, SEEK_CUR) == 0;
    close(fd);
    fd = open(SHRINK, O_RDWR);
    if (fd < 0) return false;
    errno = 0;
    ok = ok && ftruncate(fd, -1) == -1 && errno == EINVAL;
    errno = 0;
    ok = ok && ftruncate(fd, 2048) == -1 && errno == EOPNOTSUPP;
    close(fd);
    // This fixture is still fully allocated and sized before its append tests.
    fd = open(APPEND, O_RDWR);
    if (fd < 0) return false;
    ok = ok && lseek(fd, 0, SEEK_END) == 2048;
    errno = 0;
    ok = ok && write(fd, "x", 1) == -1 && errno == ENOSPC && lseek(fd, 0, SEEK_CUR) == 2048;
    ok = ok && read(fd, &byte, 1) == 0 && errno == ENOSPC;
    ok = ok && lseek(fd, 2049, SEEK_SET) == 2049;
    errno = 0;
    ok = ok && write(fd, "x", 1) == -1 && errno == EOPNOTSUPP && lseek(fd, 0, SEEK_CUR) == 2049;
    close(fd);
    return ok;
}

bool verify_append(const char* expected) {
    int fd = open(APPEND, O_RDONLY);
    if (fd < 0) return false;
    const size_t size = strlen(expected);
    char bytes[64];
    struct stat info{};
    bool ok = size < sizeof(bytes) && fstat(fd, &info) == 0 && info.st_size == static_cast<int64_t>(size);
    ok = ok && read(fd, bytes, sizeof(bytes)) == static_cast<ssize_t>(size) && memcmp(bytes, expected, size) == 0;
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
        fd = open(APPEND, READ_WRITE_APPEND);
        const int other = open(APPEND, READ_WRITE_APPEND);
        REQUIRE(fd >= 0 && other >= 0 && ftruncate(fd, 0) == 0);
        REQUIRE(write(fd, "first\n", 6) == 6);
        REQUIRE(fcntl(other, F_SETFL, O_APPEND) == 0 && fcntl(other, F_GETFL) == READ_WRITE_APPEND);
        REQUIRE(write(other, "second\n", 7) == 7);
        REQUIRE(lseek(fd, 0, SEEK_SET) == 0 && write(fd, "third\n", 6) == 6);
        REQUIRE(lseek(fd, 0, SEEK_CUR) == 19 && lseek(other, 0, SEEK_CUR) == 13);
        REQUIRE(fsync(fd) == 0);
        close(fd);
        close(other);
        REQUIRE(verify_append(APPEND_FIRST));
    } else if (stage == '1') {
        REQUIRE(verify_file(SHRINK, 37, true));
        REQUIRE(verify_file(REWRITE, PAYLOAD_SIZE, false));
        int fd = open(REWRITE, O_RDWR);
        REQUIRE(fd >= 0 && ftruncate(fd, 0) == 0 && fsync(fd) == 0);
        close(fd); // A second remount must see EOF zero without a later write.
        REQUIRE(verify_append(APPEND_FIRST));
        fd = open(APPEND, WRITE_APPEND);
        REQUIRE(fd >= 0 && write(fd, "boot2\n", 6) == 6 && fsync(fd) == 0);
        close(fd);
        REQUIRE(verify_append(APPEND_SECOND));
    } else {
        REQUIRE(verify_file(SHRINK, 37, true));
        REQUIRE(verify_file(REWRITE, 0, false));
        int fd = open(REWRITE, O_RDWR);
        REQUIRE(fd >= 0);
        REQUIRE(write(fd, payload, PAYLOAD_SIZE) == static_cast<ssize_t>(PAYLOAD_SIZE) && fsync(fd) == 0);
        close(fd); // Rewriting after a zero-length remount requires retained capacity.
        REQUIRE(verify_file(REWRITE, PAYLOAD_SIZE, false));
        REQUIRE(verify_append(APPEND_SECOND));
        fd = open(APPEND, WRITE_APPEND);
        REQUIRE(fd >= 0 && write(fd, "boot3\n", 6) == 6 && fsync(fd) == 0);
        close(fd);
        REQUIRE(verify_append(APPEND_THIRD));
    }
    ++stage;
    REQUIRE(lseek(stage_fd, 0, SEEK_SET) == 0 && write(stage_fd, &stage, 1) == 1 && fsync(stage_fd) == 0);
    close(stage_fd);
    printf("storage_smoke: PASS stage=%c\n", stage);
    reboot(RB_POWER_OFF);
    printf("storage_smoke: FAIL poweroff returned\n");
    return 1;
}
