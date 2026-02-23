// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::vfs {

enum class file_type { REGULAR, DIRECTORY, CHAR_DEVICE, BLOCK_DEVICE };

struct vfs_node;

struct vfs_ops {
    size_t (*read)(vfs_node* node, size_t offset, size_t size, void* buffer);
    size_t (*write)(vfs_node* node, size_t offset, size_t size, const void* buffer);
    void (*open)(vfs_node* node);
    void (*close)(vfs_node* node);
    int (*ioctl)(vfs_node* node, unsigned long request, void* argp);
    vfs_node* (*readdir)(vfs_node* node, size_t index);
    vfs_node* (*finddir)(vfs_node* node, const char* name);
};

struct vfs_node {
    char name[128];
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t inode;
    size_t length;
    file_type type;
    vfs_ops* ops;
    vfs_node* ptr; // Implementation specific
};

class vfs_manager {
public:
    static void init() noexcept;
    static vfs_node* get_root() noexcept;
    static void set_root(vfs_node* node) noexcept;

    // POSIX Syscalls
    static int sys_open(const char* path, int flags) noexcept;
    static int sys_read(int fd, void* buffer, size_t size) noexcept;
    static int sys_write(int fd, const void* buffer, size_t size) noexcept;
    static int sys_close(int fd) noexcept;
    static int sys_ioctl(int fd, unsigned long request, void* argp) noexcept;
};

} // namespace kernel::vfs
