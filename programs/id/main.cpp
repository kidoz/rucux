// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unistd.h>

int main() {
    printf("uid=%u euid=%u gid=%u egid=%u sid=%d pgid=%d\n", static_cast<unsigned int>(getuid()),
           static_cast<unsigned int>(geteuid()), static_cast<unsigned int>(getgid()),
           static_cast<unsigned int>(getegid()), static_cast<int>(getsid(0)), static_cast<int>(getpgid(0)));
    return 0;
}
