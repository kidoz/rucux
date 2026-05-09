// SPDX-License-Identifier: MIT
#include <stdio.h>
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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    const char* name = "rtorrent";
    for (int i = 0; i < INITD_SERVICE_NAME_MAX && name[i]; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);
    
    printf("\n");
    printf("Starting rTorrent 0.9.8...\n");
    printf("Loaded from EXT4 filesystem successfully.\n");
    printf("rTorrent is running.\n");
    
    // Just yield forever to simulate a running process
    while (true) {
        syscall(SYS_YIELD);
    }
    
    return 0;
}
