// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unistd.h>

namespace {

bool expect_identity(uid_t uid, uid_t euid, gid_t gid, gid_t egid) {
    if (getuid() != uid || geteuid() != euid || getgid() != gid || getegid() != egid) {
        printf("cred_smoke: identity mismatch uid=%u euid=%u gid=%u egid=%u\n",
               static_cast<unsigned int>(getuid()),
               static_cast<unsigned int>(geteuid()),
               static_cast<unsigned int>(getgid()),
               static_cast<unsigned int>(getegid()));
        return false;
    }
    return true;
}

} // namespace

int main() {
    printf("cred_smoke: starting\n");

    if (!expect_identity(0, 0, 0, 0)) {
        return 1;
    }

    int sid = setsid();
    if (sid <= 0 || getsid(0) != sid || getpgid(0) != sid) {
        printf("cred_smoke: setsid failed sid=%d getsid=%d getpgid=%d\n", sid, static_cast<int>(getsid(0)),
               static_cast<int>(getpgid(0)));
        return 1;
    }

    if (setgid(1000) != 0 || setuid(1000) != 0) {
        printf("cred_smoke: failed to drop to uid/gid 1000\n");
        return 1;
    }

    if (!expect_identity(1000, 1000, 1000, 1000)) {
        return 1;
    }

    if (seteuid(0) == 0 || setuid(0) == 0 || setegid(0) == 0 || setgid(0) == 0) {
        printf("cred_smoke: non-root regained privileged identity\n");
        return 1;
    }

    printf("cred_smoke: PASS uid=%u gid=%u sid=%d pgid=%d\n",
           static_cast<unsigned int>(getuid()),
           static_cast<unsigned int>(getgid()),
           static_cast<int>(getsid(0)),
           static_cast<int>(getpgid(0)));
    return 0;
}
