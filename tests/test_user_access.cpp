// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <cstring>
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vmm.hpp>
#include <kernel/syscall.hpp>
#include <uapi/kernel/syscalls.h>

namespace {
alignas(4096) uint8_t pages[8192];
bool present[2];
bool writable[2];
bool invoked;
constexpr uintptr_t BASE = kernel::memory::USER_BEGIN;
void reset() {
    memset(pages, 0xA5, sizeof(pages));
    present[0] = present[1] = writable[0] = writable[1] = true;
    invoked = false;
}
long reject_handler(long, long, long, long, long, long, long) {
    invoked = true;
    return -1;
}
} // namespace

namespace kernel::memory {
// Only address translation is simulated. Copies and syscall marshalling are
// the production implementations; fake VAs cannot be dereferenced by the host.
uintptr_t vmm::get_user_phys(uintptr_t address, bool write) noexcept {
    if (address < BASE || address >= BASE + sizeof(pages)) return 0;
    size_t offset = address - BASE;
    if (!present[offset / 4096] || (write && !writable[offset / 4096])) return 0;
    return reinterpret_cast<uintptr_t>(pages + offset);
}
} // namespace kernel::memory

TEST(copy_across_pages) {
    reset();
    char in[] = "boundary";
    char out[sizeof(in)]{};
    void* user = reinterpret_cast<void*>(BASE + 4093);
    ASSERT(kernel::memory::copy_to_user(user, in, sizeof(in)));
    ASSERT(kernel::memory::copy_from_user(out, user, sizeof(out)));
    ASSERT(memcmp(in, out, sizeof(in)) == 0);
}
TEST(readonly_output_is_atomic) {
    reset();
    writable[1] = false;
    char input[8]{};
    ASSERT(!kernel::memory::copy_to_user(reinterpret_cast<void*>(BASE + 4093), input, sizeof(input)));
    ASSERT_EQ(pages[4093], 0xA5);
}
TEST(unmapped_input_is_rejected) {
    reset();
    present[1] = false;
    char out[8]{};
    ASSERT(!kernel::memory::copy_from_user(out, reinterpret_cast<void*>(BASE + 4093), sizeof(out)));
}
TEST(kernel_and_overflow_ranges) {
    reset();
    char out[8]{};
    ASSERT(!kernel::memory::copy_from_user(out, reinterpret_cast<void*>(4096), 8));
    ASSERT(!kernel::memory::copy_from_user(out, reinterpret_cast<void*>(BASE), SIZE_MAX));
    ASSERT(kernel::memory::copy_from_user(nullptr, nullptr, 0));
}
TEST(bounded_user_string) {
    reset();
    char out[8]{};
    ASSERT_EQ(kernel::memory::copy_user_string(out, reinterpret_cast<char*>(BASE), sizeof(out)), -36);
    memcpy(pages + 4094, "abc", 4);
    ASSERT_EQ(kernel::memory::copy_user_string(out, reinterpret_cast<char*>(BASE + 4094), sizeof(out)), 3);
    ASSERT(strcmp(out, "abc") == 0);
}
TEST(invalid_syscall_buffer_never_reaches_backend) {
    reset();
    ASSERT_EQ(kernel::checked_syscall(reject_handler, SYS_WRITE, 1, 0x4000, 8, 0, 0, 0), -14);
    ASSERT(!invoked);
}
TEST(negative_syscall_length) {
    reset();
    ASSERT_EQ(kernel::checked_syscall(reject_handler, SYS_WRITE, 1, BASE, -1, 0, 0, 0), -22);
    ASSERT(!invoked);
}
TEST(write_uses_snapshot) {
    reset();
    memcpy(pages, "abc", 3);
    auto handler = +[](long, long, long buf, long count, long, long, long) -> long {
        invoked = true;
        pages[0] = 'X';
        return count == 3 && memcmp(reinterpret_cast<void*>(buf), "abc", 3) == 0 ? 3 : -1;
    };
    ASSERT_EQ(kernel::checked_syscall(handler, SYS_WRITE, 1, BASE, 3, 0, 0, 0), 3);
    ASSERT(invoked);
}
TEST(read_copies_only_returned_bytes) {
    reset();
    auto handler = +[](long, long, long buf, long, long, long, long) -> long {
        memcpy(reinterpret_cast<void*>(buf), "ok", 2);
        return 2;
    };
    ASSERT_EQ(kernel::checked_syscall(handler, SYS_READ, 0, BASE, 8, 0, 0, 0), 2);
    ASSERT_EQ(pages[0], 'o');
    ASSERT_EQ(pages[2], 0xA5);
}
TEST(unmap_while_blocked_is_rechecked) {
    reset();
    auto handler = +[](long, long, long buf, long, long, long, long) -> long {
        memset(reinterpret_cast<void*>(buf), 0, 8);
        present[0] = false;
        return 8;
    };
    ASSERT_EQ(kernel::checked_syscall(handler, SYS_READ, 0, BASE, 8, 0, 0, 0), -14);
    ASSERT_EQ(pages[0], 0xA5);
}
TEST(ipc_receive_copies_to_caller_after_blocking) {
    reset();
    auto handler = +[](long, long buf, long, long, long, long, long) -> long {
        memset(reinterpret_cast<void*>(buf), 0, 40);
        return 0;
    };
    ASSERT_EQ(kernel::checked_syscall(handler, SYS_IPC_RECV, BASE, 0, 0, 0, 0, 0), 0);
    ASSERT_EQ(pages[39], 0);
    ASSERT_EQ(pages[40], 0xA5);
}
TEST(socket_address_respects_short_capacity) {
    reset();
    *reinterpret_cast<uint32_t*>(pages + 64) = 2;
    auto handler = +[](long, long, long buf, long len, long, long, long) -> long {
        memset(reinterpret_cast<void*>(buf), 0, 16);
        *reinterpret_cast<uint32_t*>(len) = 16;
        return 0;
    };
    ASSERT_EQ(kernel::checked_syscall(handler, SYS_GETSOCKNAME, 0, BASE, BASE + 64, 0, 0, 0), 0);
    ASSERT_EQ(pages[1], 0);
    ASSERT_EQ(pages[2], 0xA5);
    ASSERT_EQ(*reinterpret_cast<uint32_t*>(pages + 64), 16U);
}
TEST(waitpid_status_is_copied_back) {
    reset();
    auto handler = +[](long, long pid, long status, long, long, long, long) -> long {
        *reinterpret_cast<int*>(status) = 7 << 8;
        return pid;
    };
    ASSERT_EQ(kernel::checked_syscall(handler, SYS_WAITPID, 42, BASE, 0, 0, 0, 0), 42);
    ASSERT_EQ(*reinterpret_cast<int*>(pages), 7 << 8);
}
TEST(waitpid_rejects_kernel_status_pointer) {
    reset();
    ASSERT_EQ(kernel::checked_syscall(reject_handler, SYS_WAITPID, -1, 0x4000, 0, 0, 0, 0), -14);
    ASSERT(!invoked);
}
int main() {
    return rucux_test::run_all_tests("User access and syscall marshalling");
}
