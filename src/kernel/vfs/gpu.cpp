// SPDX-License-Identifier: MIT
#include <kernel/console.hpp>
#include <kernel/memory/heap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/print.hpp>
#include <kernel/vfs/gpu.hpp>
#include <lib/string.hpp>
#include <uapi/kernel/gpu.h>

namespace kernel::vfs::gpu {

static uint8_t* g_backbuffer = nullptr;
static uintptr_t g_backbuffer_phys = 0;
static size_t g_buffer_size = 0;

struct gem_object {
    uint32_t handle;
    size_t size;
    uintptr_t phys_addr;
};
static constexpr size_t MAX_GEMS = 64;
static gem_object g_gems[MAX_GEMS];
static uint32_t g_next_gem_handle = 1;

static uintptr_t gem_mmap(vfs_node* node, size_t offset) {
    gem_object* gem = reinterpret_cast<gem_object*>(node->ptr);
    if (!gem || offset >= gem->size) return 0;
    return gem->phys_addr + offset;
}

static void gem_close(vfs_node* node) {
    delete node;
}

static vfs_ops gem_ops = {.read = nullptr,
                          .write = nullptr,
                          .truncate = nullptr,
                          .open = nullptr,
                          .close = gem_close,
                          .ioctl = nullptr,
                          .readdir = nullptr,
                          .finddir = nullptr,
                          .mmap = gem_mmap,
                          .fsync = nullptr,
                          .poll = nullptr};

static int gpu_ioctl(vfs_node*, unsigned long request, void* arg) {
    auto info = kernel::console::get_display_info();

    if (request == GPU_IOCTL_GET_INFO) {
        if (!arg) return -1;
        gpu_info* out = static_cast<gpu_info*>(arg);
        out->width = info.width_pixels;
        out->height = info.height_pixels;
        out->pitch = info.width_pixels * 4; // Assume 32bpp
        out->bpp = 32;
        out->buffer_size = out->pitch * out->height;
        return 0;
    }

    if (request == GPU_IOCTL_SWAP_BUFFERS) {
        if (g_backbuffer) {
            kernel::console::swap_buffers(g_backbuffer);
            return 0;
        }
        return -1;
    }

    if (request == GPU_IOCTL_GEM_CREATE) {
        if (!arg) return -1;
        gpu_gem_create* args = static_cast<gpu_gem_create*>(arg);
        for (size_t i = 0; i < MAX_GEMS; ++i) {
            if (g_gems[i].handle == 0) {
                size_t pages = (args->size + 4095) / 4096;
                g_gems[i].phys_addr = reinterpret_cast<uintptr_t>(kernel::memory::pmm::alloc_pages(pages));
                g_gems[i].size = pages * 4096;
                g_gems[i].handle = g_next_gem_handle++;
                args->handle = g_gems[i].handle;
                return 0;
            }
        }
        return -1;
    }

    if (request == GPU_IOCTL_GEM_MMAP) {
        if (!arg) return -1;
        gpu_gem_mmap* args = static_cast<gpu_gem_mmap*>(arg);
        for (size_t i = 0; i < MAX_GEMS; ++i) {
            if (g_gems[i].handle == args->handle) {
                vfs_node* node = new vfs_node();
                node->name = "gem";
                node->name_hash = vfs_node::hash_name("gem");
                node->inode = 0;
                node->length = g_gems[i].size;
                node->type = file_type::CHAR_DEVICE;
                node->ops = &gem_ops;
                node->ptr = reinterpret_cast<vfs_node*>(&g_gems[i]);

                int fd = kernel::vfs::vfs_manager::install_fd(node, 2); // O_RDWR
                if (fd >= 0) {
                    args->fd = fd;
                    return 0;
                }
                delete node;
                return -1;
            }
        }
        return -1;
    }

    return -1;
}

static uintptr_t gpu_mmap(vfs_node*, size_t offset) {
    if (offset >= g_buffer_size) return 0;
    return g_backbuffer_phys + offset;
}

static vfs_ops gpu_ops = {.read = nullptr,
                          .write = nullptr,
                          .truncate = nullptr,
                          .open = nullptr,
                          .close = nullptr,
                          .ioctl = gpu_ioctl,
                          .readdir = nullptr,
                          .finddir = nullptr,
                          .mmap = gpu_mmap,
                          .fsync = nullptr,
                          .poll = nullptr};

vfs_node* create() noexcept {
    auto info = kernel::console::get_display_info();
    if (!info.available) {
        return nullptr;
    }

    g_buffer_size = info.width_pixels * 4 * info.height_pixels;
    if (g_buffer_size > 0 && !g_backbuffer) {
        size_t pages = (g_buffer_size + 4095) / 4096;
        g_backbuffer_phys = reinterpret_cast<uintptr_t>(kernel::memory::pmm::alloc_pages(pages));
        // Need a virtual mapping for kernel access, but for now we just return physical to mmap
        // It's a stub anyway.
    }

    vfs_node* node = new vfs_node();
    node->name = "gpu0"; // Use static string since node struct doesn't own it
    node->name_hash = vfs_node::hash_name("gpu0");
    node->inode = 0;
    node->type = file_type::CHAR_DEVICE;
    node->ops = &gpu_ops;
    node->ptr = nullptr;

    kernel::print("DRM/GPU Stub initialized: {}x{}\n", info.width_pixels, info.height_pixels);

    return node;
}

uint8_t* get_backbuffer() noexcept {
    return g_backbuffer;
}

} // namespace kernel::vfs::gpu
