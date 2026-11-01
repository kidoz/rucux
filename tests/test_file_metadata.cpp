// SPDX-License-Identifier: MIT
// Actual custom-libc metadata wrappers; only raw syscalls and errno are replaced.
#include "test_harness.hpp"
#include <climits>
#include <uapi/kernel/syscalls.h>

struct rucux_stat;
extern "C" int rucux_stat(const char*, struct rucux_stat*);
extern "C" int rucux_open(const char*, int, ...);
extern "C" int rucux_fstat(int, struct rucux_stat*);
extern "C" long rucux_lseek(int, long, int);

namespace {
long result, number, arguments[6];
int error_number;
void reset() {
    result = 0;
    error_number = 123;
}
} // namespace

extern "C" int* rucux_test_errno_location() {
    return &error_number;
}
extern "C" long rucux_test_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    number = num;
    const long values[] = {a1, a2, a3, a4, a5, a6};
    for (unsigned i = 0; i < 6; ++i)
        arguments[i] = values[i];
    return result;
}

TEST(libc_metadata_forwards_paths_descriptors_and_output_buffers) {
    reset();
    char storage[120]{};
    auto* buffer = reinterpret_cast<struct rucux_stat*>(storage);
    const char* path = "/file";
    ASSERT_EQ(rucux_stat(path, buffer), 0);
    ASSERT_EQ(number, SYS_STAT);
    ASSERT_EQ(arguments[0], reinterpret_cast<long>(path));
    ASSERT_EQ(arguments[1], reinterpret_cast<long>(buffer));
    ASSERT_EQ(error_number, 123);
    ASSERT_EQ(rucux_fstat(7, buffer), 0);
    ASSERT_EQ(number, SYS_FSTAT);
    ASSERT_EQ(arguments[0], 7);
    ASSERT_EQ(arguments[1], reinterpret_cast<long>(buffer));
    ASSERT_EQ(error_number, 123);
}

TEST(libc_metadata_translates_refresh_and_validation_errors) {
    reset();
    for (long error : {2, 5, 9, 14, 16}) {
        result = -error;
        ASSERT_EQ(rucux_stat("/file", nullptr), -1);
        ASSERT_EQ(error_number, error);
        ASSERT_EQ(rucux_fstat(7, nullptr), -1);
        ASSERT_EQ(error_number, error);
    }
}

TEST(libc_lseek_preserves_full_width_offsets_and_errno_on_success) {
    reset();
    result = LONG_MAX;
    ASSERT_EQ(rucux_lseek(7, LONG_MAX, 2), LONG_MAX);
    ASSERT_EQ(number, SYS_LSEEK);
    ASSERT_EQ(arguments[0], 7);
    ASSERT_EQ(arguments[1], LONG_MAX);
    ASSERT_EQ(arguments[2], 2);
    ASSERT_EQ(error_number, 123);
}

TEST(libc_lseek_translates_refresh_and_overflow_errors) {
    reset();
    for (long error : {5, 9, 16, 22, 75}) {
        result = -error;
        ASSERT_EQ(rucux_lseek(7, -1, 2), -1L);
        ASSERT_EQ(error_number, error);
    }
}

TEST(libc_open_forwards_path_flags_and_preserves_success_errno) {
    reset();
    const char* path = "/file";
    for (long fd : {0, 7}) {
        result = fd;
        ASSERT_EQ(rucux_open(path, 2 | 0x0400), fd);
        ASSERT_EQ(number, SYS_OPEN);
        ASSERT_EQ(arguments[0], reinterpret_cast<long>(path));
        ASSERT_EQ(arguments[1], 2 | 0x0400);
        ASSERT_EQ(error_number, 123);
    }
    // The optional mode does not change the existing non-creating syscall ABI.
    ASSERT_EQ(rucux_open(path, 0x0040, 0600), 7);
    ASSERT_EQ(arguments[1], 0x0040);
}

TEST(libc_open_translates_lookup_and_descriptor_errors) {
    reset();
    for (long error : {2, 5, 12, 14, 16, 20, 22, 24, 36, 95}) {
        result = -error;
        ASSERT_EQ(rucux_open("/file", 0), -1);
        ASSERT_EQ(error_number, error);
    }
}

int main() {
    return RUN_ALL_TESTS("Custom libc file metadata");
}
