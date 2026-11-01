// SPDX-License-Identifier: MIT
// Actual libc mmap wrapper with only the raw syscall and errno storage replaced.
#include "test_harness.hpp"
#include <cstdint>
#include <uapi/kernel/syscalls.h>

extern "C" void* rucux_mmap(void*, size_t, int, int, int, long);
namespace {
long result, number, arguments[6];
int error_number;
void reset() {
    result = 0x12345000;
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
TEST(libc_mmap_forwards_all_six_arguments_and_preserves_errno_on_success) {
    reset();
    auto* address = reinterpret_cast<void*>(0x10000000);
    ASSERT_EQ(rucux_mmap(address, 8192, 1, 2, 7, 4096), reinterpret_cast<void*>(result));
    ASSERT_EQ(number, SYS_MMAP);
    ASSERT_EQ(arguments[0], reinterpret_cast<long>(address));
    ASSERT_EQ(arguments[1], 8192);
    ASSERT_EQ(arguments[2], 1);
    ASSERT_EQ(arguments[3], 2);
    ASSERT_EQ(arguments[4], 7);
    ASSERT_EQ(arguments[5], 4096);
    ASSERT_EQ(error_number, 123);
}
TEST(libc_mmap_translates_storage_resource_and_validation_errors) {
    reset();
    for (long error : {5, 9, 12, 13, 16, 17, 19, 22, 75}) {
        result = -error;
        ASSERT_EQ(rucux_mmap(nullptr, 4096, 1, 2, 7, 0), reinterpret_cast<void*>(-1L));
        ASSERT_EQ(error_number, error);
    }
}
TEST(libc_mmap_does_not_misclassify_sign_extended_armv7_user_addresses) {
    reset();
    result = -0x80000000L;
    ASSERT_EQ(rucux_mmap(nullptr, 4096, 1, 0x22, -1, 0), reinterpret_cast<void*>(result));
    ASSERT_EQ(error_number, 123);
    ASSERT_EQ(arguments[4], -1);
}
TEST(libc_mmap_checks_the_reserved_error_range_boundary) {
    reset();
    result = -4095;
    ASSERT_EQ(rucux_mmap(nullptr, 4096, 1, 2, 7, 0), reinterpret_cast<void*>(-1L));
    ASSERT_EQ(error_number, 4095);
    result = -4096;
    error_number = 123;
    ASSERT_EQ(rucux_mmap(nullptr, 4096, 1, 2, 7, 0), reinterpret_cast<void*>(result));
    ASSERT_EQ(error_number, 123);
}
int main() {
    return RUN_ALL_TESTS("Custom libc mmap errno and address results");
}
