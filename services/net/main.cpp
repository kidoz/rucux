// SPDX-License-Identifier: MIT
#include <stdio.h>
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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Register "net" service with init (PID 1)
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    const char* name = "net";
    for (int i = 0; i < INITD_SERVICE_NAME_MAX && name[i]; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    trace_producer_handle tracer = {};
    tracer.fd = -1;
    trace_producer_register("net", TRACED_PRODUCER_CATEGORY_SERVICE, &tracer);

    while (true) {
        message req = {};
        syscall(SYS_IPC_RECV, (long)&req, 0, 0);
        trace_producer_emit(&tracer, TRACE_PRODUCER_EVENT_NET_REQUEST, req.type, req.sender);

        message resp = {};
        resp.type = req.type; // Echo the type back as ACK
        resp.data[0] = -1;    // Default to error
        resp.data[1] = 0;
        resp.data[2] = 0;
        resp.data[3] = 0;

        switch (req.type) {
        case SYS_SOCKET:
            resp.data[0] = 100;
            break;
        case SYS_BIND:
            resp.data[0] = 0; // Success
            break;
        case SYS_LISTEN:
            resp.data[0] = 0; // Success
            break;
        case SYS_ACCEPT:
            resp.data[0] = 101; // Fake accepted socket
            break;
        case SYS_CONNECT:
            resp.data[0] = 0; // Success
            break;
        case SYS_SEND:
            resp.data[0] = req.data[2]; // Echo length
            break;
        case SYS_RECV:
            resp.data[0] = 0; // EOF
            break;
        default:
            break;
        }

        trace_producer_emit(&tracer, TRACE_PRODUCER_EVENT_NET_RESPONSE, resp.type, resp.data[0]);

        syscall(SYS_IPC_SEND, req.sender, (long)&resp);
    }

    return 0;
}
