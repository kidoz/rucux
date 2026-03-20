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

// POSIX poll event flags
#define POLLIN  0x001
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020

struct pollfd {
    int fd;
    short events;
    short revents;
};

// fd_set is a bitmask of 1024 file descriptors
struct fd_set_kernel {
    unsigned long fds_bits[1024 / (8 * sizeof(unsigned long))];
};

static bool fd_isset(int fd, fd_set_kernel* set) {
    if (!set || fd < 0) return false;
    size_t word = static_cast<size_t>(fd) / (8 * sizeof(unsigned long));
    size_t bit = static_cast<size_t>(fd) % (8 * sizeof(unsigned long));
    return (set->fds_bits[word] >> bit) & 1;
}

static void fd_set_bit(int fd, fd_set_kernel* set) {
    if (!set || fd < 0) return;
    size_t word = static_cast<size_t>(fd) / (8 * sizeof(unsigned long));
    size_t bit = static_cast<size_t>(fd) % (8 * sizeof(unsigned long));
    set->fds_bits[word] |= (1UL << bit);
}

static void fd_zero(fd_set_kernel* set) {
    if (!set) return;
    for (auto& w : set->fds_bits) w = 0;
}

int vfs_manager::sys_select(int nfds, void* readfds, void* writefds, void* exceptfds, void* timeout) noexcept {
    (void)timeout; // TODO: implement timeout via timer
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    auto* rfds = static_cast<fd_set_kernel*>(readfds);
    auto* wfds = static_cast<fd_set_kernel*>(writefds);
    auto* efds = static_cast<fd_set_kernel*>(exceptfds);

    // Save the input sets and zero the output sets
    fd_set_kernel r_in{}, w_in{};
    if (rfds) { r_in = *rfds; fd_zero(rfds); }
    if (wfds) { w_in = *wfds; fd_zero(wfds); }
    if (efds) fd_zero(efds);

    int ready = 0;
    for (int fd = 0; fd < nfds && fd < static_cast<int>(t->fd_count); ++fd) {
        auto& fdesc = t->fd_table[fd];
        if (!fdesc.node) continue;

        vfs_node* node = static_cast<vfs_node*>(fdesc.node);
        int events = 0;
        if (node->ops && node->ops->poll)
            events = node->ops->poll(node);
        else
            events = POLLIN | POLLOUT; // Default: always ready for regular files

        if (fd_isset(fd, &r_in) && (events & POLLIN))  { fd_set_bit(fd, rfds); ready++; }
        if (fd_isset(fd, &w_in) && (events & POLLOUT)) { fd_set_bit(fd, wfds); ready++; }
    }

    return ready;
}

int vfs_manager::sys_poll(void* fds_ptr, unsigned int nfds, int timeout) noexcept {
    (void)timeout; // TODO: implement timeout via timer
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    auto* pfds = static_cast<pollfd*>(fds_ptr);
    int ready = 0;

    for (unsigned int i = 0; i < nfds; ++i) {
        pfds[i].revents = 0;
        int fd = pfds[i].fd;
        if (fd < 0 || static_cast<size_t>(fd) >= t->fd_count) {
            pfds[i].revents = POLLNVAL;
            ready++;
            continue;
        }

        auto& fdesc = t->fd_table[fd];
        if (!fdesc.node) {
            pfds[i].revents = POLLNVAL;
            ready++;
            continue;
        }

        vfs_node* node = static_cast<vfs_node*>(fdesc.node);
        int events = 0;
        if (node->ops && node->ops->poll)
            events = node->ops->poll(node);
        else
            events = POLLIN | POLLOUT;

        pfds[i].revents = static_cast<short>(events & (pfds[i].events | POLLERR | POLLHUP));
        if (pfds[i].revents) ready++;
    }

    return ready;
}

int vfs_manager::sys_epoll_create(int size) noexcept { (void)size; return -1; }
int vfs_manager::sys_epoll_ctl(int epfd, int op, int fd, void* event) noexcept { (void)epfd; (void)op; (void)fd; (void)event; return -1; }
int vfs_manager::sys_epoll_wait(int epfd, void* events, int maxevents, int timeout) noexcept { (void)epfd; (void)events; (void)maxevents; (void)timeout; return -1; }

} // namespace kernel::vfs
