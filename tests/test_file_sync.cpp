// SPDX-License-Identifier: MIT
// Actual custom-libc wrappers, with only the raw syscall and errno storage replaced.
#include "test_harness.hpp"
#include <uapi/kernel/syscalls.h>

extern "C" int rucux_fsync(int);
extern "C" int rucux_ftruncate(int, long);
namespace {
long result, number, arguments[6];
unsigned calls;
int error_number;
void reset() {
    result = calls = 0;
    error_number = 123;
}
} // namespace
extern "C" int* rucux_test_errno_location() {
    return &error_number;
}
extern "C" long rucux_test_syscall(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    ++calls;
    number = num;
    long values[] = {a1, a2, a3, a4, a5, a6};
    for (unsigned i = 0; i < 6; ++i)
        arguments[i] = values[i];
    return result;
}
TEST(libc_fsync_forwards_arguments_and_preserves_errno_on_success) {
    reset();
    ASSERT_EQ(rucux_fsync(7), 0);
    ASSERT_EQ(number, SYS_FSYNC);
    ASSERT_EQ(arguments[0], 7);
    ASSERT_EQ(error_number, 123);
}
TEST(libc_fsync_translates_kernel_errors) {
    reset();
    for (long error : {5, 9, 16, 22}) {
        result = -error;
        ASSERT_EQ(rucux_fsync(7), -1);
        ASSERT_EQ(error_number, error);
    }
}
TEST(libc_ftruncate_forwards_length_and_translates_errors) {
    reset();
    ASSERT_EQ(rucux_ftruncate(3, 700), 0);
    ASSERT_EQ(number, SYS_FTRUNCATE);
    ASSERT_EQ(arguments[0], 3);
    ASSERT_EQ(arguments[1], 700);
    ASSERT_EQ(error_number, 123);
    result = -95;
    ASSERT_EQ(rucux_ftruncate(3, 701), -1);
    ASSERT_EQ(error_number, 95);
}
TEST(libc_negative_truncate_length_is_rejected_before_the_syscall) {
    reset();
    ASSERT_EQ(rucux_ftruncate(3, -1), -1);
    ASSERT_EQ(error_number, 22);
    ASSERT_EQ(calls, 0U);
}
int main() {
    return RUN_ALL_TESTS("Custom libc fsync and ftruncate");
}
