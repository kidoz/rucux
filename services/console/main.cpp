// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <sys/trace_producer.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/trace_producer.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

#if defined(__arm__)
void uart_putc(char c) {
    write(STDOUT_FILENO, &c, 1);
}
#else
static constexpr uint16_t COM1 = 0x3F8;

void uart_putc(char c) {
    while (!(syscall(SYS_INB, COM1 + 5) & 0x20))
        ;
    syscall(SYS_OUTB, COM1, static_cast<long>(c));
}
#endif

int main() {
    // Register as "console" with init (PID 1)
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    const char* name = "console";
    for (int i = 0; i < INITD_SERVICE_NAME_MAX && name[i]; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    // Wait for ack
    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    trace_producer_handle tracer = {};
    tracer.fd = -1;
    trace_producer_register("console", TRACED_PRODUCER_CATEGORY_DRIVER, &tracer);

    while (true) {
        message m = {};
        syscall(SYS_IPC_RECV, (long)&m, 0, 0);
        if (m.type == 1) {
            trace_producer_emit(&tracer, TRACE_PRODUCER_EVENT_CONSOLE_TX, m.data[0], m.sender);
            uart_putc(static_cast<char>(m.data[0]));
        }
    }
    return 0;
}
