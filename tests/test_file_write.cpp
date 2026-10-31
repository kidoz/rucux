// SPDX-License-Identifier: MIT
// Actual custom-libc write/fcntl wrappers with raw syscall and errno stubs.
#include "test_harness.hpp"
#include <uapi/kernel/syscalls.h>

extern "C" long rucux_write(int, const void*, size_t);
extern "C" int rucux_fcntl(int, int, ...);
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
TEST(libc_write_forwards_bytes_and_short_results_without_changing_errno) {
    reset();
    const char bytes[] = "abc";
    result = 2;
    ASSERT_EQ(rucux_write(7, bytes, 3), 2L);
    ASSERT_EQ(number, SYS_WRITE);
    ASSERT_EQ(arguments[0], 7);
    ASSERT_EQ(arguments[1], reinterpret_cast<long>(bytes));
    ASSERT_EQ(arguments[2], 3);
    ASSERT_EQ(error_number, 123);
}
TEST(libc_write_translates_access_contention_and_storage_errors) {
    reset();
    for (long error : {5, 9, 16, 28, 75, 95}) {
        result = -error;
        ASSERT_EQ(rucux_write(7, "a", 1), -1L);
        ASSERT_EQ(error_number, error);
    }
}
TEST(libc_fcntl_getfl_needs_no_variadic_argument) {
    reset();
    result = 2 | 0x0400;
    ASSERT_EQ(rucux_fcntl(7, 3), 2 | 0x0400);
    ASSERT_EQ(number, SYS_FCNTL);
    ASSERT_EQ(arguments[0], 7);
    ASSERT_EQ(arguments[1], 3);
    ASSERT_EQ(arguments[2], 0);
    ASSERT_EQ(error_number, 123);
}
TEST(libc_fcntl_setfl_forwards_an_int_and_translates_errors) {
    reset();
    ASSERT_EQ(rucux_fcntl(7, 4, 0x0400), 0);
    ASSERT_EQ(arguments[2], 0x0400);
    ASSERT_EQ(error_number, 123);
    for (long error : {9, 22}) {
        result = -error;
        ASSERT_EQ(rucux_fcntl(7, 4, 0), -1);
        ASSERT_EQ(error_number, error);
    }
}
int main() {
    return RUN_ALL_TESTS("Custom libc write and status flags");
}
