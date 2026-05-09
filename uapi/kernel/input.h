// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#define RUCUX_INPUT_PROTOCOL_VERSION 1u

enum rucux_input_status : uint32_t {
    RUCUX_INPUT_OK = 0,
    RUCUX_INPUT_ERR_INVALID = 1,
    RUCUX_INPUT_ERR_UNSUPPORTED = 2,
    RUCUX_INPUT_ERR_NOENT = 3,
    RUCUX_INPUT_ERR_BUSY = 4,
};

enum rucux_input_op : uint32_t {
    RUCUX_INPUT_OP_ATTACH_CLIENT = 1,
    RUCUX_INPUT_OP_DETACH_CLIENT = 2,
    RUCUX_INPUT_OP_SET_FOCUS_SURFACE = 3,
    RUCUX_INPUT_OP_NEXT_EVENT = 4,
    RUCUX_INPUT_OP_ACK_EVENT = 5,
};

enum rucux_input_event_type : uint16_t {
    RUCUX_INPUT_EVENT_NONE = 0,
    RUCUX_INPUT_EVENT_KEY = 1,
    RUCUX_INPUT_EVENT_POINTER_MOTION = 2,
    RUCUX_INPUT_EVENT_POINTER_BUTTON = 3,
    RUCUX_INPUT_EVENT_POINTER_AXIS = 4,
    RUCUX_INPUT_EVENT_FOCUS = 5,
};

enum rucux_input_key_state : uint16_t {
    RUCUX_INPUT_KEY_RELEASED = 0,
    RUCUX_INPUT_KEY_PRESSED = 1,
    RUCUX_INPUT_KEY_REPEATED = 2,
};

enum rucux_input_button_state : uint16_t {
    RUCUX_INPUT_BUTTON_RELEASED = 0,
    RUCUX_INPUT_BUTTON_PRESSED = 1,
};

enum rucux_input_modifiers : uint32_t {
    RUCUX_INPUT_MOD_SHIFT = 1u << 0,
    RUCUX_INPUT_MOD_CTRL = 1u << 1,
    RUCUX_INPUT_MOD_ALT = 1u << 2,
    RUCUX_INPUT_MOD_SUPER = 1u << 3,
    RUCUX_INPUT_MOD_CAPS_LOCK = 1u << 4,
    RUCUX_INPUT_MOD_NUM_LOCK = 1u << 5,
};

struct rucux_input_key_event {
    uint32_t keycode;
    uint16_t state;
    uint16_t reserved;
    uint32_t utf32;
    uint32_t modifiers;
};

struct rucux_input_pointer_motion_event {
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
};

struct rucux_input_pointer_button_event {
    uint32_t button;
    uint16_t state;
    uint16_t reserved;
    uint32_t modifiers;
};

struct rucux_input_pointer_axis_event {
    int32_t delta_x;
    int32_t delta_y;
};

struct rucux_input_focus_event {
    uint32_t surface_id;
    uint32_t focused;
};

struct rucux_input_event {
    uint64_t timestamp_ns;
    uint32_t serial;
    uint16_t type;
    uint16_t reserved;
    union {
        struct rucux_input_key_event key;
        struct rucux_input_pointer_motion_event motion;
        struct rucux_input_pointer_button_event button;
        struct rucux_input_pointer_axis_event axis;
        struct rucux_input_focus_event focus;
    } payload;
};
