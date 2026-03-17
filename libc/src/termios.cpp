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

int tcflush(int fd, int queue_selector) {
    (void)fd; (void)queue_selector;
    return 0; // stub
}

speed_t cfgetospeed(const struct termios *termios_p) {
    return termios_p->c_ospeed;
}

speed_t cfgetispeed(const struct termios *termios_p) {
    return termios_p->c_ispeed;
}

int cfsetospeed(struct termios *termios_p, speed_t speed) {
    termios_p->c_ospeed = speed;
    return 0;
}

int cfsetispeed(struct termios *termios_p, speed_t speed) {
    termios_p->c_ispeed = speed;
    return 0;
}

} // extern "C"
