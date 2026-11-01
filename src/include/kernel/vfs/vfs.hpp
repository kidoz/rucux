// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/atomic.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::scheduler {
struct thread;
}

namespace kernel::vfs {

enum class file_type { REGULAR, DIRECTORY, CHAR_DEVICE, BLOCK_DEVICE, SOCKET, EPOLL };

struct vfs_node;

struct vfs_ops {
    // Return an acknowledged byte count or negative errno. Zero is EOF/no-op,
    // never a storage error. A successful prefix takes precedence over errors.
    long (*read)(vfs_node* node, size_t offset, size_t size, void* buffer);
    long (*write)(vfs_node* node, size_t offset, size_t size, const void* buffer);
    // Return 0 or a negative errno. Truncate changes size, never the FD offset.
    int (*truncate)(vfs_node* node, size_t length);
    void (*open)(vfs_node* node);
    void (*close)(vfs_node* node);
    int (*ioctl)(vfs_node* node, unsigned long request, void* argp);
    vfs_node* (*readdir)(vfs_node* node, size_t index); // Borrowed namespace entry.
    // Return 0 and one owned node reference, or negative errno and a null result.
    int (*lookup)(vfs_node* node, const char* name, vfs_node*& result);
    uintptr_t (*mmap)(vfs_node* node, size_t offset); // Returns physical address
    int (*fsync)(vfs_node* node);
    // poll: returns bitmask of ready events (POLLIN=1, POLLOUT=4)
    int (*poll)(vfs_node* node);
    // Optional for filesystems with externally mutable cached attributes.
    // Return 0 or negative errno; leave cached attributes unchanged on failure.
    int (*refresh_metadata)(vfs_node* node) = nullptr;
    // Select EOF and write within one filesystem reservation. Return bytes or
    // negative errno; set end_offset only for a positive acknowledged prefix.
    long (*append)(vfs_node* node, size_t size, const void* buffer, size_t& end_offset) = nullptr;
};

struct vfs_node {
    const char* name;   // Static or filesystem-owned; destroy decides how to free.
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

    // Null destroy leaves lifetime to the namespace or legacy open/close hooks.
    // Managed creators publish a finalizer and one initial reference. Their
    // close hook must not free the node; destroy runs once and must not sleep.
    atomic<uint32_t> references{1};
    void (*destroy)(vfs_node* node) = nullptr;

    // The caller must already own a reference (or hold a namespace/FD pin).
    void retain() noexcept {
        if (destroy) references.fetch_add(1, relaxed);
    }
    void release() noexcept {
        if (destroy && references.fetch_sub(1, acq_rel) == 1) destroy(this);
    }
    void open_reference() noexcept {
        retain();
        if (ops && ops->open) ops->open(this);
    }
    void close_reference() noexcept {
        // Legacy permanent nodes may manage their own lifetime in close.
        const bool managed = destroy != nullptr;
        if (ops && ops->close) ops->close(this);
        if (managed) release();
    }

    static uint32_t hash_name(const char* s) noexcept {
        uint32_t h = 2166136261u; // FNV offset basis
        for (; *s; ++s)
            h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
        return h;
    }
};

// Adopts one reference; use retain() explicitly when starting from a borrow.
class node_ref {
public:
    node_ref() noexcept = default;
    explicit node_ref(vfs_node* node) noexcept
        : node_(node) {}
    ~node_ref() noexcept { reset(); }
    node_ref(const node_ref&) = delete;
    node_ref& operator=(const node_ref&) = delete;
    vfs_node* get() const noexcept { return node_; }
    void reset(vfs_node* node = nullptr) noexcept {
        auto* previous = node_;
        node_ = node;
        if (previous) previous->release();
    }
    vfs_node* detach() noexcept {
        auto* result = node_;
        node_ = nullptr;
        return result;
    }

private:
    vfs_node* node_ = nullptr;
};

class vfs_manager {
public:
    static void init() noexcept;
    // Borrowed root. set_root consumes one reference and releases the old root.
    static vfs_node* get_root() noexcept;
    static void set_root(vfs_node* node) noexcept;
    // Successful lookup/resolve returns one owned reference.
    // Absolute paths only; 1024 bytes including NUL, components at most 127 bytes.
    static int lookup(vfs_node* directory, const char* name, vfs_node*& result) noexcept;
    static int resolve_path(const char* path, vfs_node*& result) noexcept;

    // FD Management
    // Retains node on success; the caller keeps its reference.
    static int alloc_fd(vfs_node* node) noexcept;
    static void free_fd(int fd) noexcept;          // Closes and releases the entry.
    static vfs_node* get_fd_node(int fd) noexcept; // Borrowed until close.
    // Only the owning thread or an unpublished/terminated table may be changed.
    static void close_all(scheduler::thread* owner) noexcept;
    static int copy_fds(const scheduler::thread* source, scheduler::thread* target) noexcept;

    // POSIX Syscalls
    static int sys_open(const char* path, int flags) noexcept;
    static long sys_read(int fd, void* buffer, size_t size) noexcept;
    static long sys_write(int fd, const void* buffer, size_t size) noexcept;
    static int sys_close(int fd) noexcept;
    static int sys_ioctl(int fd, unsigned long request, void* argp) noexcept;
    static int sys_getdents(int fd, void* dirp, size_t count) noexcept;

    // File Management
    static long sys_lseek(int fd, long offset, int whence) noexcept;
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
