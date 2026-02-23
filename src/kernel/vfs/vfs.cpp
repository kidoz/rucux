// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>

namespace kernel::vfs {

static vfs_node* g_root = nullptr;

void vfs_manager::init() noexcept {
    kernel::print("VFS initialized\n");
}

vfs_node* vfs_manager::get_root() noexcept {
    return g_root;
}

void vfs_manager::set_root(vfs_node* node) noexcept {
    g_root = node;
}

static vfs_node* resolve_path(const char* path) {
    if (!g_root || path[0] != '/') return nullptr;

    vfs_node* current = g_root;
    char buffer[128];
    size_t path_idx = 1;

    while (path[path_idx] != '\0') {
        size_t buf_idx = 0;
        while (path[path_idx] != '/' && path[path_idx] != '\0' && buf_idx < 127) {
            buffer[buf_idx++] = path[path_idx++];
        }
        buffer[buf_idx] = '\0';

        if (path[path_idx] == '/') path_idx++;

        if (buf_idx > 0) {
            if (!current->ops || !current->ops->finddir) return nullptr;
            current = current->ops->finddir(current, buffer);
            if (!current) return nullptr;
        }
    }
    return current;
}

int vfs_manager::sys_open(const char* path, int flags) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    vfs_node* node = resolve_path(path);
    if (!node) return -1; // File not found

    if (node->ops && node->ops->open) {
        node->ops->open(node);
    }

    // Find free FD
    for (int i = 0; i < 32; i++) {
        if (t->fd_table[i].node == nullptr) {
            t->fd_table[i].node = node;
            t->fd_table[i].offset = 0;
            t->fd_table[i].flags = flags;
            return i;
        }
    }
    return -1; // Too many open files
}

int vfs_manager::sys_read(int fd, void* buffer, size_t size) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t || fd < 0 || fd >= 32) return -1;

    auto& fdesc = t->fd_table[fd];
    if (!fdesc.node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc.node);
    if (!node->ops || !node->ops->read) return -1;

    int bytes_read = node->ops->read(node, fdesc.offset, size, buffer);
    if (bytes_read > 0) {
        fdesc.offset += bytes_read;
    }
    return bytes_read;
}

int vfs_manager::sys_write(int fd, const void* buffer, size_t size) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t || fd < 0 || fd >= 32) return -1;

    auto& fdesc = t->fd_table[fd];
    if (!fdesc.node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc.node);
    if (!node->ops || !node->ops->write) return -1;

    int bytes_written = node->ops->write(node, fdesc.offset, size, buffer);
    if (bytes_written > 0) {
        fdesc.offset += bytes_written;
    }
    return bytes_written;
}

int vfs_manager::sys_close(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t || fd < 0 || fd >= 32) return -1;

    auto& fdesc = t->fd_table[fd];
    if (!fdesc.node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc.node);
    if (node->ops && node->ops->close) {
        node->ops->close(node);
    }

    fdesc.node = nullptr;
    return 0;
}

int vfs_manager::sys_ioctl(int fd, unsigned long request, void* argp) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t || fd < 0 || fd >= 32) return -1;

    auto& fdesc = t->fd_table[fd];
    if (!fdesc.node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc.node);
    if (node->ops && node->ops->ioctl) {
        return node->ops->ioctl(node, request, argp);
    }

    return -1; // ENOTTY or similar
}

} // namespace kernel::vfs
