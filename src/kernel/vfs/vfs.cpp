// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/vfs.hpp>
#include <knew.hpp>
#include <lib/string.hpp>
#include <uapi/kernel/stat.h>

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

vfs_node* vfs_manager::resolve_path(const char* path) noexcept {
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

int vfs_manager::alloc_fd(vfs_node* node) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    if (!t->fd_table) t->ensure_fd_capacity(scheduler::thread::INITIAL_FDS);

    for (size_t i = 0; i < t->fd_count; ++i) {
        if (t->fd_table[i].node == nullptr) {
            t->fd_table[i].node = node;
            t->fd_table[i].offset = 0;
            t->fd_table[i].flags = 0;
            return static_cast<int>(i);
        }
    }
    if (t->fd_count < scheduler::thread::MAX_FDS) {
        size_t old = t->fd_count;
        t->ensure_fd_capacity(t->fd_count + 1);
        if (old < t->fd_count) {
            t->fd_table[old].node = node;
            t->fd_table[old].offset = 0;
            t->fd_table[old].flags = 0;
            return static_cast<int>(old);
        }
    }
    return -1;
}

void vfs_manager::free_fd(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (fdesc) fdesc->node = nullptr;
}

vfs_node* vfs_manager::get_fd_node(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    return fdesc ? static_cast<vfs_node*>(fdesc->node) : nullptr;
}

int vfs_manager::sys_open(const char* path, int flags) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    vfs_node* node = resolve_path(path);
    if (!node) return -1;

    if (node->ops && node->ops->open) node->ops->open(node);

    int fd = alloc_fd(node);
    if (fd >= 0) {
        get_fd(t, fd)->flags = flags;
    }
    return fd;
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

// getdents: read one dirent from a directory fd.
// Uses fdesc->offset as the child index, increments on each call.
// Returns sizeof(dirent_k) on success, 0 on end-of-dir, -1 on error.
struct dirent_k {
    unsigned long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

int vfs_manager::sys_getdents(int fd, void* dirp, size_t count) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node || count < sizeof(dirent_k)) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (node->type != file_type::DIRECTORY || !node->ops || !node->ops->readdir) return -1;

    vfs_node* child = node->ops->readdir(node, fdesc->offset);
    if (!child) return 0; // End of directory

    auto* de = static_cast<dirent_k*>(dirp);
    de->d_ino = child->inode;
    de->d_off = static_cast<long>(fdesc->offset);
    de->d_reclen = sizeof(dirent_k);
    de->d_type = (child->type == file_type::DIRECTORY) ? 4 : 8; // DT_DIR=4, DT_REG=8

    // Copy name
    const char* name = child->name;
    int i = 0;
    while (name[i] && i < 255) {
        de->d_name[i] = name[i];
        i++;
    }
    de->d_name[i] = '\0';

    fdesc->offset++;
    return static_cast<int>(sizeof(dirent_k));
}

int vfs_manager::sys_lseek(int fd, long offset, int whence) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    long new_offset = 0;
    if (whence == 0)
        new_offset = offset;
    else if (whence == 1)
        new_offset = static_cast<long>(fdesc->offset) + offset;
    else if (whence == 2)
        new_offset = static_cast<long>(node->length) + offset;
    else
        return -1;

    if (new_offset < 0) return -1;
    fdesc->offset = static_cast<size_t>(new_offset);
    return new_offset;
}

static void fill_stat(const vfs_node* node, void* buffer) noexcept {
    struct stat result{};
    constexpr uint32_t types[] = {0100000, 0040000, 0020000, 0060000, 0140000, 0100000};
    result.st_ino = node->inode;
    result.st_mode = types[static_cast<unsigned>(node->type)] | (node->mask & 07777);
    result.st_nlink = 1;
    result.st_uid = node->uid;
    result.st_gid = node->gid;
    result.st_size = static_cast<int64_t>(node->length);
    result.st_blksize = 4096;
    result.st_blocks = static_cast<int64_t>(node->length / 512 + (node->length % 512 != 0));
    lib::memcpy(buffer, &result, sizeof(result));
}

int vfs_manager::sys_stat(const char* path, void* statbuf) noexcept {
    vfs_node* node = resolve_path(path);
    if (!node || !statbuf) return -1;
    fill_stat(node, statbuf);
    return 0;
}

int vfs_manager::sys_fstat(int fd, void* statbuf) noexcept {
    auto* node = get_fd_node(fd);
    if (!node || !statbuf) return -1;
    fill_stat(node, statbuf);
    return 0;
}

int vfs_manager::sys_ftruncate(int fd, long length) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -1;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (length < 0) return -1;
    if (static_cast<size_t>(length) < node->length) node->length = static_cast<size_t>(length);
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

    if (cmd == 3) return fdesc->flags; // F_GETFL
    if (cmd == 4) {
        fdesc->flags = static_cast<int>(arg);
        return 0;
    } // F_SETFL
    return -1;
}

// POSIX poll event flags
#define POLLIN 0x001
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
    for (auto& w : set->fds_bits)
        w = 0;
}

int vfs_manager::sys_select(int nfds, void* readfds, void* writefds, void* exceptfds, void* timeout) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    auto* rfds = static_cast<fd_set_kernel*>(readfds);
    auto* wfds = static_cast<fd_set_kernel*>(writefds);
    auto* efds = static_cast<fd_set_kernel*>(exceptfds);

    // Save the input sets
    fd_set_kernel r_in{}, w_in{}, e_in{};
    if (rfds) r_in = *rfds;
    if (wfds) w_in = *wfds;
    if (efds) e_in = *efds;

    uint64_t deadline = 0;
    bool has_timeout = false;
    if (timeout) {
        auto* tv = static_cast<kernel::timeval*>(timeout);
        deadline = kernel::time_manager::get_ticks() + (tv->tv_sec * 1000) + (tv->tv_usec / 1000);
        has_timeout = true;
    }

    while (true) {
        int ready = 0;
        if (rfds) fd_zero(rfds);
        if (wfds) fd_zero(wfds);
        if (efds) fd_zero(efds);

        for (int fd = 0; fd < nfds && fd < static_cast<int>(t->fd_count); ++fd) {
            auto& fdesc = t->fd_table[fd];
            if (!fdesc.node) continue;

            vfs_node* node = static_cast<vfs_node*>(fdesc.node);
            int events = 0;
            if (node->ops && node->ops->poll)
                events = node->ops->poll(node);
            else
                events = POLLIN | POLLOUT; // Default: always ready for regular files

            if (fd_isset(fd, &r_in) && (events & POLLIN)) {
                fd_set_bit(fd, rfds);
                ready++;
            }
            if (fd_isset(fd, &w_in) && (events & POLLOUT)) {
                fd_set_bit(fd, wfds);
                ready++;
            }
            if (fd_isset(fd, &e_in) && (events & (POLLERR | POLLHUP | POLLNVAL))) {
                fd_set_bit(fd, efds);
                ready++;
            }
        }

        if (ready > 0) return ready;

        if (has_timeout && kernel::time_manager::get_ticks() >= deadline) {
            return 0; // timeout
        }

        scheduler::scheduler::sleep_until(kernel::time_manager::get_ticks() + 1);
    }
}

int vfs_manager::sys_poll(void* fds_ptr, unsigned int nfds, int timeout) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -1;

    auto* pfds = static_cast<pollfd*>(fds_ptr);

    uint64_t deadline = 0;
    bool has_timeout = (timeout >= 0);
    if (has_timeout) {
        deadline = kernel::time_manager::get_ticks() + timeout;
    }

    while (true) {
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

        if (ready > 0) return ready;

        if (has_timeout && kernel::time_manager::get_ticks() >= deadline) {
            return 0; // timeout
        }

        scheduler::scheduler::sleep_until(kernel::time_manager::get_ticks() + 1);
    }
}

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

struct epoll_entry {
    int fd;
    epoll_event event;
    epoll_entry* next;
};

struct epoll_node_data {
    epoll_entry* head;
};

int vfs_manager::sys_epoll_create(int size) noexcept {
    (void)size;
    auto* node = new vfs_node();
    lib::memset(node, 0, sizeof(*node));
    node->type = file_type::EPOLL;

    // allocate data
    auto* data = new epoll_node_data();
    data->head = nullptr;
    node->ptr = reinterpret_cast<vfs_node*>(data);

    // ops
    static vfs_ops epoll_ops = {};
    epoll_ops.close = [](vfs_node* n) {
        auto* d = reinterpret_cast<epoll_node_data*>(n->ptr);
        auto* curr = d->head;
        while (curr) {
            auto* next = curr->next;
            delete curr;
            curr = next;
        }
        delete d;
        delete n;
    };
    node->ops = &epoll_ops;

    int fd = alloc_fd(node);
    if (fd < 0) {
        epoll_ops.close(node);
        return -1;
    }
    return fd;
}

int vfs_manager::sys_epoll_ctl(int epfd, int op, int fd, void* event_ptr) noexcept {
    auto* epnode = get_fd_node(epfd);
    if (!epnode || epnode->type != file_type::EPOLL) return -1;

    auto* d = reinterpret_cast<epoll_node_data*>(epnode->ptr);
    auto* ev = static_cast<epoll_event*>(event_ptr);

    if (op == EPOLL_CTL_ADD) {
        auto* entry = new epoll_entry{fd, *ev, d->head};
        d->head = entry;
        return 0;
    } else if (op == EPOLL_CTL_DEL) {
        epoll_entry** pp = &d->head;
        while (*pp) {
            if ((*pp)->fd == fd) {
                auto* to_del = *pp;
                *pp = (*pp)->next;
                delete to_del;
                return 0;
            }
            pp = &(*pp)->next;
        }
        return -1; // ENOENT
    } else if (op == EPOLL_CTL_MOD) {
        auto* curr = d->head;
        while (curr) {
            if (curr->fd == fd) {
                curr->event = *ev;
                return 0;
            }
            curr = curr->next;
        }
        return -1;
    }
    return -1;
}

int vfs_manager::sys_epoll_wait(int epfd, void* events_ptr, int maxevents, int timeout) noexcept {
    auto* epnode = get_fd_node(epfd);
    if (!epnode || epnode->type != file_type::EPOLL) return -1;

    auto* d = reinterpret_cast<epoll_node_data*>(epnode->ptr);
    auto* out_events = static_cast<epoll_event*>(events_ptr);

    uint64_t deadline = 0;
    bool has_timeout = (timeout >= 0);
    if (has_timeout) {
        deadline = kernel::time_manager::get_ticks() + timeout;
    }

    auto* t = scheduler::scheduler::current_thread();

    while (true) {
        int ready = 0;
        auto* curr = d->head;
        while (curr && ready < maxevents) {
            int fd = curr->fd;
            auto* fdesc = get_fd(t, fd);
            if (fdesc && fdesc->node) {
                vfs_node* node = static_cast<vfs_node*>(fdesc->node);
                int status = 0;
                if (node->ops && node->ops->poll)
                    status = node->ops->poll(node);
                else
                    status = POLLIN | POLLOUT;

                uint32_t revents = 0;
                if ((status & POLLIN) && (curr->event.events & EPOLLIN)) revents |= EPOLLIN;
                if ((status & POLLOUT) && (curr->event.events & EPOLLOUT)) revents |= EPOLLOUT;
                if (status & (POLLERR | POLLHUP | POLLNVAL)) revents |= EPOLLERR;

                if (revents) {
                    out_events[ready].events = revents;
                    out_events[ready].data = curr->event.data;
                    ready++;
                }
            }
            curr = curr->next;
        }

        if (ready > 0) return ready;

        if (has_timeout && kernel::time_manager::get_ticks() >= deadline) {
            return 0;
        }

        scheduler::scheduler::sleep_until(kernel::time_manager::get_ticks() + 1);
    }
}

} // namespace kernel::vfs
