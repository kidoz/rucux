// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {
static long __syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}
}

// Simple US QWERTY translation table (Set 1 scancodes)
static const char kbd_us[128] = {
    0,    27,  '1', '2',  '3', '4', '5', '6', '7',  '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
    't',  'y', 'u', 'i',  'o', 'p', '[', ']', '\n', 0,   'a', 's', 'd', 'f', 'g',  'h',  'j', 'k', 'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', 'b',  'n', 'm', ',', '.', '/', 0,    '*',  0,   ' ', 0,   0,
    0,    0,   0,   0,    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,   0,   0,   0,
    0,    0,   0,   0,    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,   0};

int main() {
    printf("KBD Driver Initialized. Pumping to /dev/tty via TIOCSTI...\n");

    while (true) {
        __syscall(SYS_IRQ_WAIT, 1, 0, 0);
        uint8_t scancode = static_cast<uint8_t>(__syscall(SYS_INB, 0x60, 0, 0));

        // If it's a key press (top bit not set)
        if (!(scancode & 0x80)) {
            char ascii = kbd_us[scancode & 0x7F];
            if (ascii) {
                ioctl(0, TIOCSTI, &ascii);
            }
        }
    }
    return 0;
}
