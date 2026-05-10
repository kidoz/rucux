// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::vfs {

enum class file_type { REGULAR, DIRECTORY, CHAR_DEVICE, BLOCK_DEVICE, SOCKET, EPOLL };

struct vfs_node;

struct vfs_ops {
    size_t (*read)(vfs_node* node, size_t offset, size_t size, void* buffer);
    size_t (*write)(vfs_node* node, size_t offset, size_t size, const void* buffer);
    void (*open)(vfs_node* node);
    void (*close)(vfs_node* node);
    int (*ioctl)(vfs_node* node, unsigned long request, void* argp);
    vfs_node* (*readdir)(vfs_node* node, size_t index);
    vfs_node* (*finddir)(vfs_node* node, const char* name);
    uintptr_t (*mmap)(vfs_node* node, size_t offset); // Returns physical address
    // poll: returns bitmask of ready events (POLLIN=1, POLLOUT=4)
    int (*poll)(vfs_node* node);
};

struct vfs_node {
    const char* name;  // Dynamically allocated name string
    uint32_t name_hash; // FNV-1a hash for fast comparison
    uint32_t inode;
    size_t length;
    file_type type;
    uint16_t uid;
    uint16_t gid;
    uint16_t mask;
    uint16_t flags;
    vfs_ops* ops;
    vfs_node* ptr; // Implementation specific

    static uint32_t hash_name(const char* s) noexcept {
        uint32_t h = 2166136261u; // FNV offset basis
        for (; *s; ++s)
            h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
        return h;
    }
};

class vfs_manager {
public:
    static void init() noexcept;
    static vfs_node* get_root() noexcept;
    static void set_root(vfs_node* node) noexcept;
    static vfs_node* resolve_path(const char* path) noexcept;

    // FD Management
    static int alloc_fd(vfs_node* node) noexcept;
    static void free_fd(int fd) noexcept;
    static vfs_node* get_fd_node(int fd) noexcept;

    // POSIX Syscalls
    static int sys_open(const char* path, int flags) noexcept;
    static int sys_read(int fd, void* buffer, size_t size) noexcept;
    static int sys_write(int fd, const void* buffer, size_t size) noexcept;
    static int sys_close(int fd) noexcept;
    static int sys_ioctl(int fd, unsigned long request, void* argp) noexcept;
    static int sys_getdents(int fd, void* dirp, size_t count) noexcept;

    // File Management
    static int sys_lseek(int fd, long offset, int whence) noexcept;
    static int sys_stat(const char* path, void* statbuf) noexcept;
    static int sys_fstat(int fd, void* statbuf) noexcept;
    static int sys_ftruncate(int fd, long length) noexcept;
    static int sys_fsync(int fd) noexcept;
    static int sys_fcntl(int fd, int cmd, long arg) noexcept;

    // Multiplexing
    static int sys_select(int nfds, void* readfds, void* writefds, void* exceptfds, void* timeout) noexcept;
    static int sys_poll(void* fds, unsigned int nfds, int timeout) noexcept;
    static int sys_epoll_create(int size) noexcept;
    static int sys_epoll_ctl(int epfd, int op, int fd, void* event) noexcept;
    static int sys_epoll_wait(int epfd, void* events, int maxevents, int timeout) noexcept;
};

} // namespace kernel::vfs
