// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define RUCUX_DISPLAY_PROTOCOL_VERSION 1u

enum rucux_display_status : uint32_t {
    RUCUX_DISPLAY_OK = 0,
    RUCUX_DISPLAY_ERR_INVALID = 1,
    RUCUX_DISPLAY_ERR_UNSUPPORTED = 2,
    RUCUX_DISPLAY_ERR_NOENT = 3,
    RUCUX_DISPLAY_ERR_BUSY = 4,
    RUCUX_DISPLAY_ERR_NOSPC = 5,
};

enum rucux_display_pixel_format : uint32_t {
    RUCUX_DISPLAY_FORMAT_XRGB8888 = 1,
    RUCUX_DISPLAY_FORMAT_ARGB8888 = 2,
    RUCUX_DISPLAY_FORMAT_RGB888 = 3,
};

enum rucux_display_mode_flags : uint32_t {
    RUCUX_DISPLAY_MODE_NONE = 0,
    RUCUX_DISPLAY_MODE_PRIMARY = 1u << 0,
};

enum rucux_display_buffer_flags : uint32_t {
    RUCUX_DISPLAY_BUFFER_NONE = 0,
    RUCUX_DISPLAY_BUFFER_CPU_VISIBLE = 1u << 0,
    RUCUX_DISPLAY_BUFFER_SCANOUT = 1u << 1,
    RUCUX_DISPLAY_BUFFER_SHARED = 1u << 2,
};

enum rucux_display_op : uint32_t {
    RUCUX_DISPLAY_OP_GET_DEFAULT_OUTPUT = 1,
    RUCUX_DISPLAY_OP_GET_MODE = 2,
    RUCUX_DISPLAY_OP_CREATE_BUFFER = 3,
    RUCUX_DISPLAY_OP_DESTROY_BUFFER = 4,
    RUCUX_DISPLAY_OP_PRESENT_BUFFER = 5,
    RUCUX_DISPLAY_OP_SET_CURSOR = 6,
    RUCUX_DISPLAY_OP_SET_MODE = 7,
};

struct rucux_display_mode {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp;
    uint32_t format;
    uint32_t flags;
};

struct rucux_display_output_info {
    uint32_t output_id;
    uint32_t reserved;
    struct rucux_display_mode mode;
};

struct rucux_display_buffer_desc {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
    uint32_t reserved;
    uint64_t size_bytes;
};

struct rucux_display_buffer_info {
    uint32_t buffer_id;
    int32_t fd;
    struct rucux_display_buffer_desc desc;
};

struct rucux_display_present_request {
    uint32_t output_id;
    uint32_t buffer_id;
    uint32_t serial;
    uint32_t flags;
};

struct rucux_display_cursor_request {
    int32_t hot_x;
    int32_t hot_y;
    uint32_t buffer_id;
    uint32_t flags;
};
