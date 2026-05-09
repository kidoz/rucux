// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define GPU_IOCTL_GET_INFO 0x4701
#define GPU_IOCTL_SWAP_BUFFERS 0x4702
#define GPU_IOCTL_GEM_CREATE 0x4703
#define GPU_IOCTL_GEM_MMAP 0x4704

struct gpu_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t buffer_size;
};

struct gpu_gem_create {
    uint32_t size;    // IN
    uint32_t handle;  // OUT
};

struct gpu_gem_mmap {
    uint32_t handle;  // IN
    int fd;           // OUT
};
