// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/top.h>
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
constexpr int READ_WRITE = static_cast<int>(static_cast<unsigned>(PROT_READ) | static_cast<unsigned>(PROT_WRITE));
constexpr int PRIVATE_ANON =
    static_cast<int>(static_cast<unsigned>(MAP_PRIVATE) | static_cast<unsigned>(MAP_ANONYMOUS));
constexpr int PRIVATE_FIXED = static_cast<int>(static_cast<unsigned>(MAP_PRIVATE) | static_cast<unsigned>(MAP_FIXED));
constexpr int ANON_FIXED = static_cast<int>(static_cast<unsigned>(PRIVATE_ANON) | static_cast<unsigned>(MAP_FIXED));
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

bool lookup_error(const char* path, int expected) {
    errno = 123;
    const int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return false;
    }
    if (fd != -1 || errno != expected) return false;
    struct stat info{};
    info.st_size = -123;
    errno = 123;
    return stat(path, &info) == -1 && errno == expected && info.st_size == -123;
}

bool verify_lookup() {
    if (!lookup_error("/fat32/missing.bin", ENOENT) || !lookup_error("/run/log/missing", ENOENT) ||
        !lookup_error("/fat32/faultdir/known.bin", EIO) || !lookup_error("/fat32/shrink.bin/child", ENOTDIR) ||
        !lookup_error("/fat32/shrink.bin/", ENOTDIR) || !lookup_error("/fat32/toolongname.bin", ENAMETOOLONG) ||
        !lookup_error("/fat32/a.b.c", EINVAL) || !lookup_error("relative", EINVAL) || !lookup_error("", ENOENT) ||
        !lookup_error("/fat32/../shrink.bin", EOPNOTSUPP))
        return false;
    char component[135] = "/run/";
    memset(component + 5, 'x', 128);
    component[133] = 0;
    if (!lookup_error(component, ENAMETOOLONG)) return false;
    char path[1025];
    memset(path, '/', 1024);
    path[1024] = 0;
    if (!lookup_error(path, ENAMETOOLONG)) return false;
    errno = 123;
    int fd = open("//fat32//shrink.bin", O_RDONLY);
    if (fd < 0) return false;
    bool ok = errno == 123;
    close(fd);
    errno = 123;
    fd = open("/fat32/", O_DIRECTORY);
    if (fd < 0) return false;
    ok = ok && errno == 123;
    close(fd);
    errno = 123;
    fd = open("/fat32/shrink.bin", O_DIRECTORY);
    if (fd >= 0) {
        close(fd);
        return false;
    }
    ok = ok && fd == -1 && errno == ENOTDIR;
    if (ok) printf("storage_smoke: lookup errno and path validation PASS\n");
    return ok;
}

struct clone_probe {
    int fd;
    unsigned ready = 0, proceed = 0, done = 0;
    bool fatal;
    bool ok = false;
};

void inherited_file_probe(void* argument) {
    auto* probe = static_cast<clone_probe*>(argument);
    __atomic_store_n(&probe->ready, 1U, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&probe->proceed, __ATOMIC_ACQUIRE))
        sched_yield();
    struct stat info{};
    // The parent's descriptor is already closed. Leave both child descriptors
    // open so ordinary/fatal thread exit must release them.
    probe->ok = fstat(probe->fd, &info) == 0 && info.st_size > 0;
    const int extra = open("/fat32/mmap.bin", O_RDONLY);
    probe->ok = probe->ok && extra >= 0;
    __atomic_store_n(&probe->done, 1U, __ATOMIC_RELEASE);
    if (probe->fatal) {
        for (;;)
            sched_yield();
    }
    _exit(probe->ok ? 0 : 1);
}

bool verify_ownership() {
    constexpr size_t STACK = 64 * 1024, PAGE = 4096;
    for (unsigned i = 0; i < 32; ++i) {
        struct stat info{};
        if (stat(SHRINK, &info) < 0 || !lookup_error("/fat32/missing.bin", ENOENT)) return false;
        const int fd = open("/fat32/mmap.bin", O_RDONLY);
        if (fd < 0) return false;
        const auto* mapping = static_cast<const uint8_t*>(mmap(nullptr, PAGE, PROT_READ, MAP_PRIVATE, fd, 0));
        close(fd);
        if (mapping == MAP_FAILED) return false;
        const bool ok = mapping[0] == 11 && mapping[PAGE - 1] == static_cast<uint8_t>((PAGE - 1) * 7 + 11);
        if (munmap(const_cast<uint8_t*>(mapping), PAGE) != 0 || !ok) return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        const int fd = open("/fat32/mmap.bin", O_RDONLY);
        if (fd < 0) return false;
        auto* stack = static_cast<uint8_t*>(mmap(nullptr, STACK, READ_WRITE, PRIVATE_ANON, -1, 0));
        if (stack == MAP_FAILED) {
            close(fd);
            return false;
        }
        clone_probe probe{fd, 0, 0, 0, (i & 1U) != 0, false};
        const long tid = syscall(SYS_CLONE, reinterpret_cast<long>(inherited_file_probe),
                                 reinterpret_cast<long>(stack + STACK), reinterpret_cast<long>(&probe));
        if (tid < 0) {
            close(fd);
            munmap(stack, STACK);
            return false;
        }
        while (!__atomic_load_n(&probe.ready, __ATOMIC_ACQUIRE))
            sched_yield();
        close(fd);
        __atomic_store_n(&probe.proceed, 1U, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&probe.done, __ATOMIC_ACQUIRE))
            sched_yield();
        if (probe.fatal && kill(static_cast<int>(tid), SIGTERM) < 0) return false;
        while (kill(static_cast<int>(tid), 0) >= 0)
            sched_yield();
        if (munmap(stack, STACK) != 0 || !probe.ok) return false;
    }
    printf("storage_smoke: vnode references and thread cleanup PASS\n");
    return true;
}

struct cancellation_probe {
    unsigned operation;
    unsigned ready = 0, returned = 0;
    uint32_t word = 0;
};

void blocked_probe(void* argument) {
    auto* probe = static_cast<cancellation_probe*>(argument);
    int fd = -1;
    if (probe->operation == 2) fd = open("/dev/tty", O_RDONLY);
    if (probe->operation == 3) fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe->operation == 4) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(0x7F000001);
            address.sin_port = htons(19093);
            if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 1) != 0) _exit(1);
        }
    }
    if (probe->operation == 6) fd = epoll_create(1);
    if ((probe->operation == 2 || probe->operation == 3 || probe->operation == 4 || probe->operation == 6) && fd < 0)
        _exit(1);
    __atomic_store_n(&probe->ready, 1U, __ATOMIC_RELEASE);
    char bytes[32]{};
    switch (probe->operation) {
    case 0: {
        timespec delay{60, 0};
        syscall(SYS_NANOSLEEP, reinterpret_cast<long>(&delay), 0);
        break;
    }
    case 1:
        syscall(SYS_FUTEX, reinterpret_cast<long>(&probe->word), 0, 0);
        break;
    case 2:
    case 3:
        read(fd, bytes, sizeof(bytes));
        break;
    case 4:
        accept(fd, nullptr, nullptr);
        break;
    case 5:
        poll(nullptr, 0, -1);
        break;
    case 6: {
        epoll_event event{};
        epoll_wait(fd, &event, 1, -1);
        break;
    }
    default:
        _exit(1);
    }
    __atomic_store_n(&probe->returned, 1U, __ATOMIC_RELEASE);
    _exit(1); // A cancelled syscall must never return to userspace.
}

bool wait_blocked(long tid) {
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
        top_info info{};
        if (syscall(SYS_TOP, reinterpret_cast<long>(&info), sizeof(info)) < 0) return false;
        for (uint32_t i = 0; i < info.num_processes; ++i) {
            if (info.processes[i].tid != static_cast<uint32_t>(tid)) continue;
            if (info.processes[i].state == 2) return true;
            if (info.processes[i].state == 3) return false;
        }
        sched_yield();
    }
    return false;
}

bool verify_cancellation() {
    constexpr size_t STACK = 64 * 1024;
    for (unsigned operation = 0; operation < 7; ++operation) {
        auto* stack = static_cast<uint8_t*>(mmap(nullptr, STACK, READ_WRITE, PRIVATE_ANON, -1, 0));
        if (stack == MAP_FAILED) return false;
        cancellation_probe probe{operation, 0, 0, 0};
        const long tid = syscall(SYS_CLONE, reinterpret_cast<long>(blocked_probe),
                                 reinterpret_cast<long>(stack + STACK), reinterpret_cast<long>(&probe));
        if (tid < 0) {
            munmap(stack, STACK);
            return false;
        }
        if (!wait_blocked(tid) || !__atomic_load_n(&probe.ready, __ATOMIC_ACQUIRE)) return false;
        if (kill(static_cast<int>(tid), operation & 1U ? SIGKILL : SIGTERM) != 0) return false;
        while (kill(static_cast<int>(tid), 0) >= 0)
            sched_yield();
        if (__atomic_load_n(&probe.returned, __ATOMIC_ACQUIRE)) return false;
        if (syscall(SYS_FUTEX, reinterpret_cast<long>(&probe.word), 1, 1) != 0) return false;
        if (munmap(stack, STACK) != 0) return false;
        // The interrupted operation must not leave storage reservations or
        // unfinished VMAs that poison the next ordinary operation.
        {
            const int fd = open("/fat32/mmap.bin", O_RDONLY);
            if (fd < 0) return false;
            struct stat info{};
            uint8_t byte{};
            const bool ok = fstat(fd, &info) == 0 && info.st_size == 8192 + 37 && read(fd, &byte, 1) == 1 && byte == 11;
            close(fd);
            if (!ok) return false;
        }
    }
    printf("storage_smoke: safe fatal syscall cancellation PASS\n");
    return true;
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

bool verify_mmap() {
    constexpr size_t PAGE = 4096, SIZE = 2 * PAGE + 37;
    int fd = open("/fat32/mmap.bin", O_RDONLY);
    if (fd < 0) return false;
    bool ok = lseek(fd, 13, SEEK_SET) == 13;
    errno = 123;
    auto* mapping = static_cast<uint8_t*>(mmap(nullptr, SIZE, READ_WRITE, MAP_PRIVATE, fd, 0));
    ok = ok && mapping != MAP_FAILED && errno == 123;
    if (mapping != MAP_FAILED) {
        for (size_t i = 0; ok && i < 3 * PAGE; ++i)
            ok = mapping[i] == (i < SIZE ? static_cast<uint8_t>(i * 7 + 11) : 0);
        mapping[0] = 0; // Private copies must not write back into the fixture.
        ok = ok && munmap(mapping, SIZE) == 0;
    }
    ok = ok && lseek(fd, 0, SEEK_CUR) == 13 && lseek(fd, 0, SEEK_SET) == 0;
    uint8_t byte = 0;
    ok = ok && read(fd, &byte, 1) == 1 && byte == 11;
    close(fd);
    fd = open("/fat32/mmap.bin", O_WRONLY);
    if (fd < 0) return false;
    errno = 0;
    ok = ok && mmap(nullptr, PAGE, PROT_READ, MAP_PRIVATE, fd, 0) == MAP_FAILED && errno == EACCES;
    close(fd);
    errno = 0;
    ok = ok && mmap(nullptr, PAGE, PROT_READ, MAP_PRIVATE, -1, 0) == MAP_FAILED && errno == EBADF;
    fd = open("/fat32/mmaperr.bin", O_RDONLY);
    if (fd < 0) return false;
    void* target = mmap(nullptr, 2 * PAGE, READ_WRITE, PRIVATE_ANON, -1, 0);
    if (target == MAP_FAILED) {
        close(fd);
        return false;
    }
    ok = ok && munmap(target, 2 * PAGE) == 0;
    for (unsigned attempt = 0; ok && attempt < 16; ++attempt) {
        errno = 0;
        ok = mmap(target, 2 * PAGE, PROT_READ, PRIVATE_FIXED, fd, 0) == MAP_FAILED && errno == EIO;
        errno = 0;
        ok = ok && mmap(target, PAGE, PROT_READ, PRIVATE_FIXED, fd, PAGE) == MAP_FAILED && errno == EIO;
        auto* probe = static_cast<uint8_t*>(mmap(target, 2 * PAGE, READ_WRITE, ANON_FIXED, -1, 0));
        if (probe == MAP_FAILED) {
            ok = false;
            break;
        }
        ok = ok && probe == target && probe[0] == 0 && probe[PAGE] == 0;
        probe[0] = 1;
        probe[PAGE] = 2;
        ok = ok && munmap(probe, 2 * PAGE) == 0;
    }
    close(fd);
    if (ok) printf("storage_smoke: mmap read-error rollback PASS\n");
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
    REQUIRE(verify_lookup());
    REQUIRE(verify_ownership());
    REQUIRE(verify_cancellation());
    REQUIRE(verify_ramfs());
    REQUIRE(verify_mmap());
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
