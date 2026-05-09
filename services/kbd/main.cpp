// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

// Simple US QWERTY translation table (Set 1 scancodes)
static const char kbd_us[128] = {
    0,    27,  '1', '2',  '3', '4', '5', '6', '7',  '8', '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
    't',  'y', 'u', 'i',  'o', 'p', '[', ']', '\n', 0,   'a', 's', 'd', 'f', 'g',  'h',  'j', 'k', 'l', ';',
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', 'b',  'n', 'm', ',', '.', '/', 0,    '*',  0,   ' ', 0,   0,
    0,    0,   0,   0,    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,   0,   0,   0,
    0,    0,   0,   0,    0,   0,   0,   0,   0,    0,   0,   0,   0,   0,   0,    0,    0,   0};

int main() {
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    const char* name = "kbd";
    for (int i = 0; i < INITD_SERVICE_NAME_MAX && name[i]; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg, 0);

    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    printf("KBD Driver Initialized. Pumping to /dev/tty via TIOCSTI...\n");

    while (true) {
        syscall(SYS_IRQ_WAIT, 1, 0, 0);
        uint8_t scancode = static_cast<uint8_t>(syscall(SYS_INB, 0x60, 0, 0));

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
