// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define RUCUX_GUI_SESSION_PROTOCOL_VERSION 1u

enum rucux_gui_session_status : uint32_t {
    RUCUX_GUI_SESSION_OK = 0,
    RUCUX_GUI_SESSION_ERR_INVALID = 1,
    RUCUX_GUI_SESSION_ERR_UNSUPPORTED = 2,
    RUCUX_GUI_SESSION_ERR_NOENT = 3,
    RUCUX_GUI_SESSION_ERR_BUSY = 4,
    RUCUX_GUI_SESSION_ERR_NOSPC = 5,
};

enum rucux_gui_session_op : uint32_t {
    RUCUX_GUI_SESSION_OP_CREATE = 1,
    RUCUX_GUI_SESSION_OP_DESTROY = 2,
    RUCUX_GUI_SESSION_OP_CREATE_SURFACE = 3,
    RUCUX_GUI_SESSION_OP_DESTROY_SURFACE = 4,
    RUCUX_GUI_SESSION_OP_ATTACH_BUFFER = 5,
    RUCUX_GUI_SESSION_OP_COMMIT = 6,
    RUCUX_GUI_SESSION_OP_SET_TITLE = 7,
    RUCUX_GUI_SESSION_OP_SET_ROLE = 8,
};

enum rucux_gui_surface_role : uint32_t {
    RUCUX_GUI_SURFACE_ROLE_NONE = 0,
    RUCUX_GUI_SURFACE_ROLE_TOPLEVEL = 1,
    RUCUX_GUI_SURFACE_ROLE_POPUP = 2,
    RUCUX_GUI_SURFACE_ROLE_CURSOR = 3,
};

enum rucux_gui_surface_flags : uint32_t {
    RUCUX_GUI_SURFACE_NONE = 0,
    RUCUX_GUI_SURFACE_VISIBLE = 1u << 0,
    RUCUX_GUI_SURFACE_FOCUSED = 1u << 1,
    RUCUX_GUI_SURFACE_FULLSCREEN = 1u << 2,
};

struct rucux_gui_session_create_request {
    uint32_t client_id;
    uint32_t flags;
    uint32_t session_id;
    uint32_t reserved;
};

struct rucux_gui_session_info {
    uint32_t session_id;
    uint32_t flags;
};

struct rucux_gui_surface_create_request {
    uint32_t session_id;
    uint32_t width;
    uint32_t height;
    uint32_t role;
    uint32_t flags;
    uint32_t surface_id;
};

struct rucux_gui_surface_info {
    uint32_t surface_id;
    uint32_t session_id;
    uint32_t width;
    uint32_t height;
    uint32_t role;
    uint32_t flags;
};

struct rucux_gui_attach_buffer_request {
    uint32_t surface_id;
    uint32_t buffer_id;
    int32_t offset_x;
    int32_t offset_y;
};

struct rucux_gui_commit_request {
    uint32_t surface_id;
    uint32_t serial;
    uint32_t flags;
    uint32_t reserved;
};
