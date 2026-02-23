// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("Network Server (lwIP Proxy) Initializing...\n");

    // Register "net" service with init (PID 1)
    message reg;
    reg.type = 10; // REGISTER_SERVICE
    const char* name = "net";
    for (int i = 0; i < 8; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    printf("Network Server Registered! Listening for BSD socket IPC...\n");

    while (true) {
        message req;
        syscall(SYS_IPC_RECV, (long)&req, 0, 0);

        message resp;
        resp.type = req.type; // Echo the type back as ACK
        resp.data[0] = -1;    // Default to error
        resp.data[1] = 0;
        resp.data[2] = 0;
        resp.data[3] = 0;

        switch (req.type) {
        case SYS_SOCKET:
            printf("NET: User requested socket(domain=%d, type=%d, protocol=%d)\n", (int)req.data[0], (int)req.data[1],
                   (int)req.data[2]);
            // Fake a successful socket creation (FD 100 for now)
            resp.data[0] = 100;
            break;
        case SYS_BIND:
            printf("NET: User requested bind(sockfd=%d)\n", (int)req.data[0]);
            resp.data[0] = 0; // Success
            break;
        case SYS_LISTEN:
            printf("NET: User requested listen(sockfd=%d, backlog=%d)\n", (int)req.data[0], (int)req.data[1]);
            resp.data[0] = 0; // Success
            break;
        case SYS_ACCEPT:
            printf("NET: User requested accept(sockfd=%d)\n", (int)req.data[0]);
            resp.data[0] = 101; // Fake accepted socket
            break;
        case SYS_CONNECT:
            printf("NET: User requested connect(sockfd=%d)\n", (int)req.data[0]);
            resp.data[0] = 0; // Success
            break;
        case SYS_SEND:
            printf("NET: User requested send(sockfd=%d, len=%d)\n", (int)req.data[0], (int)req.data[2]);
            resp.data[0] = req.data[2]; // Echo length
            break;
        case SYS_RECV:
            printf("NET: User requested recv(sockfd=%d, len=%d)\n", (int)req.data[0], (int)req.data[2]);
            resp.data[0] = 0; // EOF
            break;
        default:
            printf("NET: Unknown request type %d\n", req.type);
            break;
        }

        syscall(SYS_IPC_SEND, req.sender, (long)&resp);
    }

    return 0;
}
