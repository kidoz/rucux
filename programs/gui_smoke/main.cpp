// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <uapi/kernel/display.h>
#include <uapi/kernel/gui_session.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

static long display_ctl(uint32_t op, void* arg, size_t size) {
    return syscall(SYS_DISPLAY_CTL, op, reinterpret_cast<long>(arg), static_cast<long>(size), 0, 0, 0);
}

static long gui_session_ctl(uint32_t op, void* arg, size_t size) {
    return syscall(SYS_GUI_SESSION, op, reinterpret_cast<long>(arg), static_cast<long>(size), 0, 0, 0);
}

extern "C" int main() {
    rucux_display_output_info output = {};
    if (display_ctl(RUCUX_DISPLAY_OP_GET_DEFAULT_OUTPUT, &output, sizeof(output)) != RUCUX_DISPLAY_OK) {
        printf("gui_smoke: no display available\n");
        return 1;
    }

    rucux_display_buffer_info buffer = {};
    buffer.desc.width = output.mode.width;
    buffer.desc.height = output.mode.height;
    buffer.desc.stride = output.mode.stride;
    buffer.desc.format = output.mode.format;
    buffer.desc.flags = RUCUX_DISPLAY_BUFFER_CPU_VISIBLE | RUCUX_DISPLAY_BUFFER_SCANOUT;
    buffer.desc.size_bytes = static_cast<uint64_t>(buffer.desc.stride) * buffer.desc.height;

    if (display_ctl(RUCUX_DISPLAY_OP_CREATE_BUFFER, &buffer, sizeof(buffer)) != RUCUX_DISPLAY_OK) {
        printf("gui_smoke: failed to create display buffer\n");
        return 1;
    }

    auto* pixels = static_cast<uint8_t*>(
        mmap(nullptr, static_cast<size_t>(buffer.desc.size_bytes), PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0));
    if (pixels == MAP_FAILED) {
        printf("gui_smoke: failed to mmap display buffer\n");
        return 1;
    }

    for (uint32_t y = 0; y < buffer.desc.height; ++y) {
        for (uint32_t x = 0; x < buffer.desc.width; ++x) {
            size_t offset = static_cast<size_t>(y) * buffer.desc.stride + static_cast<size_t>(x) * 4;
            pixels[offset + 0] = static_cast<uint8_t>((x * 255) / buffer.desc.width);
            pixels[offset + 1] = static_cast<uint8_t>((y * 255) / buffer.desc.height);
            pixels[offset + 2] = 0x80;
            pixels[offset + 3] = 0xff;
        }
    }

    rucux_gui_session_create_request session = {};
    if (gui_session_ctl(RUCUX_GUI_SESSION_OP_CREATE, &session, sizeof(session)) != RUCUX_GUI_SESSION_OK) {
        printf("gui_smoke: failed to create GUI session\n");
        return 1;
    }

    rucux_gui_surface_create_request surface = {};
    surface.session_id = session.session_id;
    surface.width = buffer.desc.width;
    surface.height = buffer.desc.height;
    surface.role = RUCUX_GUI_SURFACE_ROLE_TOPLEVEL;
    surface.flags = RUCUX_GUI_SURFACE_VISIBLE;
    if (gui_session_ctl(RUCUX_GUI_SESSION_OP_CREATE_SURFACE, &surface, sizeof(surface)) != RUCUX_GUI_SESSION_OK) {
        printf("gui_smoke: failed to create GUI surface\n");
        return 1;
    }

    rucux_gui_attach_buffer_request attach = {};
    attach.surface_id = surface.surface_id;
    attach.buffer_id = buffer.buffer_id;
    if (gui_session_ctl(RUCUX_GUI_SESSION_OP_ATTACH_BUFFER, &attach, sizeof(attach)) != RUCUX_GUI_SESSION_OK) {
        printf("gui_smoke: failed to attach display buffer\n");
        return 1;
    }

    rucux_gui_commit_request commit = {};
    commit.surface_id = surface.surface_id;
    commit.serial = 1;
    if (gui_session_ctl(RUCUX_GUI_SESSION_OP_COMMIT, &commit, sizeof(commit)) != RUCUX_GUI_SESSION_OK) {
        printf("gui_smoke: GUI commit failed\n");
        return 1;
    }

    printf("gui_smoke: presented %ux%u software surface\n", buffer.desc.width, buffer.desc.height);
    return 0;
}
