// SPDX-License-Identifier: MIT
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <uapi/kernel/gpu.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/initd.h>
#include <unistd.h>
#include <stdlib.h>
#include <wayland-server.h>

extern "C" {
struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};
}

static void custom_log_handler(const char* fmt, va_list ap) {
    (void)fmt; (void)ap;
    printf("WM Wayland Log\n");
}

extern "C" int main() {
    printf("Starting Wayland Compositor...\n");
    wl_log_set_handler_server(custom_log_handler);

    // Register with init
    message reg = {};
    reg.type = INITD_MSG_REGISTER_SERVICE;
    const char* name = "wm";
    for (int i = 0; i < INITD_SERVICE_NAME_MAX && name[i]; ++i)
        reinterpret_cast<char*>(reg.data)[i] = name[i];
    syscall(SYS_IPC_SEND, 1, (long)&reg);

    // Wait for ack
    message ack = {};
    syscall(SYS_IPC_RECV, (long)&ack, 0, 0);

    setenv("XDG_RUNTIME_DIR", "/run", 1);

    struct wl_display *display = wl_display_create();
    if (!display) {
        printf("WM: failed to create display\n");
        return 1;
    }
    printf("WM: wl_display_create() SUCCESS\n");

    int ret = wl_display_add_socket(display, "wayland-0");
    if (ret != 0) {
        printf("WM: failed to add socket wayland-0\n");
        return 1;
    }

    printf("WM: Running wayland server on socket: wayland-0\n");
    wl_display_run(display);

    wl_display_destroy(display);

    printf("WM compositor done.\n");
    return 0;
}