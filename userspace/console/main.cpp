// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <uapi/kernel/syscalls.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

long syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}
}

static constexpr uint16_t COM1 = 0x3F8;

void uart_putc(char c) {
    while (!(syscall(SYS_INB, COM1 + 5) & 0x20))
        ;
    syscall(SYS_OUTB, COM1, static_cast<long>(c));
}

int main() {
    // Register as "console" with init (PID 1)
    message reg;
    reg.type = 10; // REGISTER_SERVICE
    const char* name = "console";
    for (int i = 0; i < 8; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    // Wait for ack
    message ack;
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    while (true) {
        message m;
        syscall(SYS_IPC_RECV, (long)&m, 0, 0);
        if (m.type == 1) uart_putc(static_cast<char>(m.data[0]));
    }
    return 0;
}
