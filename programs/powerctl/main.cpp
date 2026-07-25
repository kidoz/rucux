// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>

namespace {

void usage(const char* argv0) {
    printf("usage: %s reboot|poweroff\n", argv0 ? argv0 : "powerctl");
}

int issue_command(int command, const char* action) {
    printf("powerctl: requesting %s\n", action);
    int rc = reboot(command);
    printf("powerctl: %s failed rc=%d\n", action, rc);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        usage(argc > 0 ? argv[0] : "powerctl");
        return 1;
    }

    if (strcmp(argv[1], "reboot") == 0 || strcmp(argv[1], "restart") == 0) {
        return issue_command(RB_AUTOBOOT, "reboot");
    }
    if (strcmp(argv[1], "poweroff") == 0 || strcmp(argv[1], "off") == 0 || strcmp(argv[1], "shutdown") == 0) {
        return issue_command(RB_POWER_OFF, "poweroff");
    }

    usage(argv[0]);
    return 1;
}
