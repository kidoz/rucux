// SPDX-License-Identifier: MIT
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

extern "C" {

int tcgetattr(int fd, struct termios* termios_p) {
    return ioctl(fd, TCGETS, termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios* termios_p) {
    // optional_actions (TCSANOW, etc.) are ignored for this simple implementation
    (void)optional_actions;
    return ioctl(fd, TCSETS, (void*)termios_p);
}

} // extern "C"
