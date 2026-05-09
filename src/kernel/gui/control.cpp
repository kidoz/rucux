// SPDX-License-Identifier: MIT
#include <kernel/gui/control.hpp>

#include <kernel/console.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/print.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>
#include <uapi/kernel/display.h>
#include <uapi/kernel/gui_session.h>
#include <uapi/kernel/input.h>

namespace kernel::gui {
namespace {

static constexpr uint32_t DEFAULT_OUTPUT_ID = 1;
static constexpr size_t MAX_DISPLAY_BUFFERS = 64;
static constexpr size_t MAX_GUI_SESSIONS = 16;
static constexpr size_t MAX_GUI_SURFACES = 64;

struct display_buffer {
    bool active;
    uint32_t id;
    uintptr_t phys;
    size_t size;
    rucux_display_buffer_desc desc;
};

struct gui_session {
    bool active;
    uint32_t id;
    uint32_t client_id;
    uint32_t flags;
};

struct gui_surface {
    bool active;
    uint32_t id;
    uint32_t session_id;
    uint32_t attached_buffer_id;
    int32_t offset_x;
    int32_t offset_y;
    uint32_t width;
    uint32_t height;
    uint32_t role;
    uint32_t flags;
};

static kernel::irq_spinlock g_lock;
static display_buffer g_buffers[MAX_DISPLAY_BUFFERS] = {};
static gui_session g_sessions[MAX_GUI_SESSIONS] = {};
static gui_surface g_surfaces[MAX_GUI_SURFACES] = {};
static uint32_t g_next_buffer_id = 1;
static uint32_t g_next_session_id = 1;
static uint32_t g_next_surface_id = 1;
static uintptr_t g_scanout_phys = 0;
static size_t g_scanout_size = 0;

static size_t page_align(size_t value) noexcept {
    return (value + kernel::memory::pmm::PAGE_SIZE - 1) & ~(kernel::memory::pmm::PAGE_SIZE - 1);
}

static bool display_available() noexcept {
    return kernel::console::get_display_info().available;
}

static rucux_display_mode current_mode() noexcept {
    auto info = kernel::console::get_display_info();
    uint32_t width = info.width_pixels;
    uint32_t height = info.height_pixels;
    return {
        .width = width,
        .height = height,
        .stride = width * 4,
        .bpp = 32,
        .format = RUCUX_DISPLAY_FORMAT_XRGB8888,
        .flags = RUCUX_DISPLAY_MODE_PRIMARY,
    };
}

static bool ensure_scanout() noexcept {
    auto mode = current_mode();
    if (mode.width == 0 || mode.height == 0) {
        return false;
    }

    size_t needed = page_align(static_cast<size_t>(mode.stride) * mode.height);
    if (g_scanout_phys != 0 && g_scanout_size >= needed) {
        return true;
    }

    void* phys = kernel::memory::pmm::alloc_pages(needed / kernel::memory::pmm::PAGE_SIZE);
    if (!phys) {
        return false;
    }

    g_scanout_phys = reinterpret_cast<uintptr_t>(phys);
    g_scanout_size = needed;
    lib::memset(reinterpret_cast<void*>(g_scanout_phys), 0, g_scanout_size);
    return true;
}

static display_buffer* find_buffer(uint32_t id) noexcept {
    for (auto& buffer : g_buffers) {
        if (buffer.active && buffer.id == id) {
            return &buffer;
        }
    }
    return nullptr;
}

static gui_session* find_session(uint32_t id) noexcept {
    for (auto& session : g_sessions) {
        if (session.active && session.id == id) {
            return &session;
        }
    }
    return nullptr;
}

static gui_surface* find_surface(uint32_t id) noexcept {
    for (auto& surface : g_surfaces) {
        if (surface.active && surface.id == id) {
            return &surface;
        }
    }
    return nullptr;
}

static uintptr_t display_buffer_mmap(kernel::vfs::vfs_node* node, size_t offset) {
    auto* buffer = reinterpret_cast<display_buffer*>(node->ptr);
    if (!buffer || !buffer->active || offset >= buffer->size) {
        return 0;
    }
    return buffer->phys + offset;
}

static void display_buffer_close(kernel::vfs::vfs_node* node) {
    delete node;
}

static kernel::vfs::vfs_ops display_buffer_ops = {
    .read = nullptr,
    .write = nullptr,
    .truncate = nullptr,
    .open = nullptr,
    .close = display_buffer_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .mmap = display_buffer_mmap,
    .fsync = nullptr,
    .poll = nullptr,
};

static int install_buffer_fd(display_buffer& buffer) noexcept {
    auto* node = new kernel::vfs::vfs_node();
    if (!node) {
        return -1;
    }

    node->name = "display-buffer";
    node->name_hash = kernel::vfs::vfs_node::hash_name("display-buffer");
    node->inode = buffer.id;
    node->length = buffer.size;
    node->type = kernel::vfs::file_type::CHAR_DEVICE;
    node->ops = &display_buffer_ops;
    node->ptr = reinterpret_cast<kernel::vfs::vfs_node*>(&buffer);

    int fd = kernel::vfs::vfs_manager::install_fd(node, 2);
    if (fd < 0) {
        delete node;
    }
    return fd;
}

static long present_buffer(display_buffer& buffer) noexcept {
    if (!display_available() || !ensure_scanout()) {
        return RUCUX_DISPLAY_ERR_NOENT;
    }

    auto mode = current_mode();
    auto* dst = reinterpret_cast<uint8_t*>(g_scanout_phys);
    auto* src = reinterpret_cast<const uint8_t*>(buffer.phys);
    const uint32_t copy_width = buffer.desc.width < mode.width ? buffer.desc.width : mode.width;
    const uint32_t copy_height = buffer.desc.height < mode.height ? buffer.desc.height : mode.height;
    const size_t copy_bytes = static_cast<size_t>(copy_width) * 4;

    lib::memset(dst, 0, g_scanout_size);
    for (uint32_t y = 0; y < copy_height; ++y) {
        lib::memcpy(dst + (static_cast<size_t>(y) * mode.stride),
                    src + (static_cast<size_t>(y) * buffer.desc.stride),
                    copy_bytes);
    }

    kernel::console::swap_buffers(dst);
    return RUCUX_DISPLAY_OK;
}

} // namespace

long sys_display_ctl(uint32_t op, void* arg, size_t arg_size, uintptr_t flags) noexcept {
    (void)flags;

    if (!display_available()) {
        return RUCUX_DISPLAY_ERR_NOENT;
    }

    switch (op) {
    case RUCUX_DISPLAY_OP_GET_DEFAULT_OUTPUT: {
        if (!arg || arg_size < sizeof(rucux_display_output_info)) {
            return RUCUX_DISPLAY_ERR_INVALID;
        }

        auto* out = static_cast<rucux_display_output_info*>(arg);
        out->output_id = DEFAULT_OUTPUT_ID;
        out->reserved = 0;
        out->mode = current_mode();
        return RUCUX_DISPLAY_OK;
    }
    case RUCUX_DISPLAY_OP_GET_MODE: {
        if (!arg || arg_size < sizeof(rucux_display_mode)) {
            return RUCUX_DISPLAY_ERR_INVALID;
        }

        *static_cast<rucux_display_mode*>(arg) = current_mode();
        return RUCUX_DISPLAY_OK;
    }
    case RUCUX_DISPLAY_OP_CREATE_BUFFER: {
        if (!arg || arg_size < sizeof(rucux_display_buffer_info)) {
            return RUCUX_DISPLAY_ERR_INVALID;
        }

        auto* info = static_cast<rucux_display_buffer_info*>(arg);
        auto mode = current_mode();
        if (info->desc.width == 0) info->desc.width = mode.width;
        if (info->desc.height == 0) info->desc.height = mode.height;
        if (info->desc.format == 0) info->desc.format = RUCUX_DISPLAY_FORMAT_XRGB8888;
        if (info->desc.stride == 0) info->desc.stride = info->desc.width * 4;
        if (info->desc.size_bytes == 0) {
            info->desc.size_bytes = static_cast<uint64_t>(info->desc.stride) * info->desc.height;
        }

        if (info->desc.format != RUCUX_DISPLAY_FORMAT_XRGB8888 &&
            info->desc.format != RUCUX_DISPLAY_FORMAT_ARGB8888) {
            return RUCUX_DISPLAY_ERR_UNSUPPORTED;
        }

        size_t alloc_size = page_align(static_cast<size_t>(info->desc.size_bytes));
        if (alloc_size == 0) {
            return RUCUX_DISPLAY_ERR_INVALID;
        }

        kernel::irq_lock_guard guard(g_lock);
        for (auto& buffer : g_buffers) {
            if (!buffer.active) {
                void* phys = kernel::memory::pmm::alloc_pages(alloc_size / kernel::memory::pmm::PAGE_SIZE);
                if (!phys) {
                    return RUCUX_DISPLAY_ERR_NOSPC;
                }

                buffer.active = true;
                buffer.id = g_next_buffer_id++;
                buffer.phys = reinterpret_cast<uintptr_t>(phys);
                buffer.size = alloc_size;
                buffer.desc = info->desc;
                buffer.desc.size_bytes = alloc_size;
                lib::memset(reinterpret_cast<void*>(buffer.phys), 0, buffer.size);

                int fd = install_buffer_fd(buffer);
                if (fd < 0) {
                    buffer.active = false;
                    return RUCUX_DISPLAY_ERR_NOSPC;
                }

                info->buffer_id = buffer.id;
                info->fd = fd;
                info->desc = buffer.desc;
                return RUCUX_DISPLAY_OK;
            }
        }

        return RUCUX_DISPLAY_ERR_NOSPC;
    }
    case RUCUX_DISPLAY_OP_DESTROY_BUFFER: {
        if (!arg || arg_size < sizeof(uint32_t)) {
            return RUCUX_DISPLAY_ERR_INVALID;
        }

        uint32_t id = *static_cast<uint32_t*>(arg);
        kernel::irq_lock_guard guard(g_lock);
        auto* buffer = find_buffer(id);
        if (!buffer) {
            return RUCUX_DISPLAY_ERR_NOENT;
        }
        buffer->active = false;
        return RUCUX_DISPLAY_OK;
    }
    case RUCUX_DISPLAY_OP_PRESENT_BUFFER: {
        if (!arg || arg_size < sizeof(rucux_display_present_request)) {
            return RUCUX_DISPLAY_ERR_INVALID;
        }

        auto* request = static_cast<rucux_display_present_request*>(arg);
        auto* buffer = find_buffer(request->buffer_id);
        if (!buffer) {
            return RUCUX_DISPLAY_ERR_NOENT;
        }
        return present_buffer(*buffer);
    }
    case RUCUX_DISPLAY_OP_SET_CURSOR:
    case RUCUX_DISPLAY_OP_SET_MODE:
        return RUCUX_DISPLAY_ERR_UNSUPPORTED;
    default:
        return RUCUX_DISPLAY_ERR_INVALID;
    }
}

long sys_gui_session(uint32_t op, void* arg, size_t arg_size, uintptr_t flags) noexcept {
    (void)flags;

    switch (op) {
    case RUCUX_GUI_SESSION_OP_CREATE: {
        if (!arg || arg_size < sizeof(rucux_gui_session_create_request)) {
            return RUCUX_GUI_SESSION_ERR_INVALID;
        }

        auto* request = static_cast<rucux_gui_session_create_request*>(arg);
        kernel::irq_lock_guard guard(g_lock);
        for (auto& session : g_sessions) {
            if (!session.active) {
                session.active = true;
                session.id = g_next_session_id++;
                session.client_id = request->client_id;
                session.flags = request->flags;
                request->session_id = session.id;
                request->reserved = 0;
                return RUCUX_GUI_SESSION_OK;
            }
        }
        return RUCUX_GUI_SESSION_ERR_NOSPC;
    }
    case RUCUX_GUI_SESSION_OP_DESTROY: {
        if (!arg || arg_size < sizeof(uint32_t)) {
            return RUCUX_GUI_SESSION_ERR_INVALID;
        }

        uint32_t id = *static_cast<uint32_t*>(arg);
        kernel::irq_lock_guard guard(g_lock);
        auto* session = find_session(id);
        if (!session) {
            return RUCUX_GUI_SESSION_ERR_NOENT;
        }

        for (auto& surface : g_surfaces) {
            if (surface.active && surface.session_id == id) {
                surface.active = false;
            }
        }
        session->active = false;
        return RUCUX_GUI_SESSION_OK;
    }
    case RUCUX_GUI_SESSION_OP_CREATE_SURFACE: {
        if (!arg || arg_size < sizeof(rucux_gui_surface_create_request)) {
            return RUCUX_GUI_SESSION_ERR_INVALID;
        }

        auto* request = static_cast<rucux_gui_surface_create_request*>(arg);
        kernel::irq_lock_guard guard(g_lock);
        if (!find_session(request->session_id)) {
            return RUCUX_GUI_SESSION_ERR_NOENT;
        }

        for (auto& surface : g_surfaces) {
            if (!surface.active) {
                surface.active = true;
                surface.id = g_next_surface_id++;
                surface.session_id = request->session_id;
                surface.width = request->width;
                surface.height = request->height;
                surface.role = request->role;
                surface.flags = request->flags;
                surface.attached_buffer_id = 0;
                surface.offset_x = 0;
                surface.offset_y = 0;
                request->surface_id = surface.id;
                return RUCUX_GUI_SESSION_OK;
            }
        }
        return RUCUX_GUI_SESSION_ERR_NOSPC;
    }
    case RUCUX_GUI_SESSION_OP_DESTROY_SURFACE: {
        if (!arg || arg_size < sizeof(uint32_t)) {
            return RUCUX_GUI_SESSION_ERR_INVALID;
        }

        uint32_t id = *static_cast<uint32_t*>(arg);
        kernel::irq_lock_guard guard(g_lock);
        auto* surface = find_surface(id);
        if (!surface) {
            return RUCUX_GUI_SESSION_ERR_NOENT;
        }
        surface->active = false;
        return RUCUX_GUI_SESSION_OK;
    }
    case RUCUX_GUI_SESSION_OP_ATTACH_BUFFER: {
        if (!arg || arg_size < sizeof(rucux_gui_attach_buffer_request)) {
            return RUCUX_GUI_SESSION_ERR_INVALID;
        }

        auto* request = static_cast<rucux_gui_attach_buffer_request*>(arg);
        kernel::irq_lock_guard guard(g_lock);
        auto* surface = find_surface(request->surface_id);
        if (!surface || !find_buffer(request->buffer_id)) {
            return RUCUX_GUI_SESSION_ERR_NOENT;
        }
        surface->attached_buffer_id = request->buffer_id;
        surface->offset_x = request->offset_x;
        surface->offset_y = request->offset_y;
        return RUCUX_GUI_SESSION_OK;
    }
    case RUCUX_GUI_SESSION_OP_COMMIT: {
        if (!arg || arg_size < sizeof(rucux_gui_commit_request)) {
            return RUCUX_GUI_SESSION_ERR_INVALID;
        }

        auto* request = static_cast<rucux_gui_commit_request*>(arg);
        auto* surface = find_surface(request->surface_id);
        if (!surface || surface->attached_buffer_id == 0) {
            return RUCUX_GUI_SESSION_ERR_NOENT;
        }

        auto* buffer = find_buffer(surface->attached_buffer_id);
        if (!buffer) {
            return RUCUX_GUI_SESSION_ERR_NOENT;
        }

        long result = present_buffer(*buffer);
        return result == RUCUX_DISPLAY_OK ? RUCUX_GUI_SESSION_OK : RUCUX_GUI_SESSION_ERR_UNSUPPORTED;
    }
    case RUCUX_GUI_SESSION_OP_SET_TITLE:
    case RUCUX_GUI_SESSION_OP_SET_ROLE:
        return RUCUX_GUI_SESSION_OK;
    default:
        return RUCUX_GUI_SESSION_ERR_INVALID;
    }
}

long sys_input_ctl(uint32_t op, void* arg, size_t arg_size, uintptr_t flags) noexcept {
    (void)arg;
    (void)arg_size;
    (void)flags;

    switch (op) {
    case RUCUX_INPUT_OP_ATTACH_CLIENT:
    case RUCUX_INPUT_OP_DETACH_CLIENT:
    case RUCUX_INPUT_OP_SET_FOCUS_SURFACE:
    case RUCUX_INPUT_OP_ACK_EVENT:
        return RUCUX_INPUT_OK;
    case RUCUX_INPUT_OP_NEXT_EVENT:
        return RUCUX_INPUT_ERR_NOENT;
    default:
        return RUCUX_INPUT_ERR_INVALID;
    }
}

} // namespace kernel::gui
