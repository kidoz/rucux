// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <uapi/kernel/gpu.h>
#include <uapi/kernel/initd.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>
#include <wayland-client.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

extern "C" int main() {
    printf("Starting Wayland Client...\n");

    // Register with init
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    const char* name = "wm_client";
    for (int i = 0; i < INITD_SERVICE_NAME_MAX && name[i]; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    // Wait for ack
    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    setenv("XDG_RUNTIME_DIR", "/run", 1);

    struct wl_display* display = nullptr;
    for (int i = 0; i < 100000; ++i) {
        display = wl_display_connect("wayland-0");
        if (display) break;
        syscall(SYS_YIELD);
    }

    if (!display) {
        printf("Client: Failed to connect to Wayland server\n");
        return 1;
    }

    printf("Client: Connected to Wayland server successfully!\n");

    // Roundtrip to process initial globals
    wl_display_roundtrip(display);

    printf("Client: Completed roundtrip. Wayland wire protocol is active!\n");

    wl_display_disconnect(display);

    printf("WM Client done.\n");
    return 0;
}