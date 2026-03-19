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

// Helper: get fd entry, ensuring capacity exists. Returns nullptr on bad fd.
using fd_t = scheduler::thread::file_descriptor;
static fd_t* get_fd(scheduler::thread* t, int fd) {
    if (!t || fd < 0 || static_cast<size_t>(fd) >= t->fd_count) return nullptr;
    return &t->fd_table[fd];
}

int vfs_manager::sys_open(const char* path, int flags) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    vfs_node* node = resolve_path(path);
    if (!node) return -1;

    if (node->ops && node->ops->open)
        node->ops->open(node);

    // Ensure we have an FD table
    if (!t->fd_table)
        t->ensure_fd_capacity(scheduler::thread::INITIAL_FDS);

    // Find free FD, growing if needed
    for (size_t i = 0; i < t->fd_count; ++i) {
        if (t->fd_table[i].node == nullptr) {
            t->fd_table[i].node = node;
            t->fd_table[i].offset = 0;
            t->fd_table[i].flags = flags;
            return static_cast<int>(i);
        }
    }
    // All slots full — try to grow
    if (t->fd_count < scheduler::thread::MAX_FDS) {
        size_t old = t->fd_count;
        t->ensure_fd_capacity(t->fd_count + 1);
        if (old < t->fd_count) {
            t->fd_table[old].node = node;
            t->fd_table[old].offset = 0;
            t->fd_table[old].flags = flags;
            return static_cast<int>(old);
        }
    }
    return -1; // EMFILE
}

int vfs_manager::sys_read(int fd, void* buffer, size_t size) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (!node->ops || !node->ops->read) return -1;

    int bytes_read = node->ops->read(node, fdesc->offset, size, buffer);
    if (bytes_read > 0) fdesc->offset += bytes_read;
    return bytes_read;
}

int vfs_manager::sys_write(int fd, const void* buffer, size_t size) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (!node->ops || !node->ops->write) return -1;

    int bytes_written = node->ops->write(node, fdesc->offset, size, buffer);
    if (bytes_written > 0) fdesc->offset += bytes_written;
    return bytes_written;
}

int vfs_manager::sys_close(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (node->ops && node->ops->close) node->ops->close(node);

    fdesc->node = nullptr;
    return 0;
}

int vfs_manager::sys_ioctl(int fd, unsigned long request, void* argp) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (node->ops && node->ops->ioctl) return node->ops->ioctl(node, request, argp);
    return -1;
}

int vfs_manager::sys_lseek(int fd, long offset, int whence) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    long new_offset = 0;
    if (whence == 0)      new_offset = offset;
    else if (whence == 1) new_offset = static_cast<long>(fdesc->offset) + offset;
    else if (whence == 2) new_offset = static_cast<long>(node->length) + offset;
    else return -1;

    if (new_offset < 0) return -1;
    fdesc->offset = static_cast<size_t>(new_offset);
    return new_offset;
}

int vfs_manager::sys_stat(const char* path, void* statbuf) noexcept {
    vfs_node* node = resolve_path(path);
    if (!node) return -1;
    lib::memset(statbuf, 0, 144);
    return 0;
}

int vfs_manager::sys_fstat(int fd, void* statbuf) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;
    lib::memset(statbuf, 0, 144);
    return 0;
}

int vfs_manager::sys_ftruncate(int fd, long length) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (length < 0) return -1;
    if (static_cast<size_t>(length) < node->length)
        node->length = static_cast<size_t>(length);
    return 0;
}

int vfs_manager::sys_fsync(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;
    return 0;
}

int vfs_manager::sys_fcntl(int fd, int cmd, long arg) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    if (cmd == 3) return fdesc->flags;       // F_GETFL
    if (cmd == 4) { fdesc->flags = static_cast<int>(arg); return 0; } // F_SETFL
    return -1;
}

int vfs_manager::sys_select(int nfds, void* readfds, void* writefds, void* exceptfds, void* timeout) noexcept {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
    return -1;
}

int vfs_manager::sys_poll(void* fds, unsigned int nfds, int timeout) noexcept {
    (void)fds; (void)nfds; (void)timeout;
    return -1;
}

int vfs_manager::sys_epoll_create(int size) noexcept { (void)size; return -1; }
int vfs_manager::sys_epoll_ctl(int epfd, int op, int fd, void* event) noexcept { (void)epfd; (void)op; (void)fd; (void)event; return -1; }
int vfs_manager::sys_epoll_wait(int epfd, void* events, int maxevents, int timeout) noexcept { (void)epfd; (void)events; (void)maxevents; (void)timeout; return -1; }

} // namespace kernel::vfs
