// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <kernel/process/credentials.hpp>
#include <uapi/kernel/credentials.h>

namespace {

kernel::process::credentials identity(uint32_t uid) {
    kernel::process::credentials creds{};
    creds.ruid = creds.euid = creds.suid = uid;
    creds.rgid = creds.egid = creds.sgid = uid;
    return creds;
}

} // namespace

TEST(root_capabilities_exclude_raw_io) {
    ASSERT((kernel::process::ROOT_CAPABILITIES & RUCUX_CAP_RAW_IO) == 0);
    ASSERT((kernel::process::ROOT_CAPABILITIES & RUCUX_CAP_POWER) != 0);
    kernel::process::credentials driver = identity(0);
    driver.capabilities = RUCUX_CAP_RAW_IO;
    ASSERT(kernel::process::has_capability(driver, RUCUX_CAP_RAW_IO));
    ASSERT(!kernel::process::has_capability(driver, RUCUX_CAP_POWER));
    ASSERT(!kernel::process::has_capability(driver, 0));
}

TEST(signal_permission_follows_uid) {
    auto root = identity(0);
    auto user = identity(1000);
    auto other = identity(1001);
    ASSERT(kernel::process::may_signal(user, 4, user, 4));
    ASSERT(kernel::process::may_signal(user, 4, user, 5));
    ASSERT(!kernel::process::may_signal(user, 4, other, 6));
    ASSERT(kernel::process::may_signal(root, 1, other, 6));
    other.ruid = 1000;
    other.euid = 1001;
    ASSERT(kernel::process::may_signal(user, 4, other, 6));
}

TEST(ipc_allows_root_servers_and_same_user) {
    auto root = identity(0);
    auto user = identity(1000);
    auto other = identity(1001);
    ASSERT(kernel::process::may_ipc(user, user));
    ASSERT(kernel::process::may_ipc(user, root));
    ASSERT(kernel::process::may_ipc(root, other));
    ASSERT(!kernel::process::may_ipc(user, other));
}

int main() {
    return rucux_test::run_all_tests("Credential authorization");
}
