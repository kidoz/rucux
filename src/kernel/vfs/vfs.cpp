// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/vfs.hpp>
#include <knew.hpp>
#include <lib/string.hpp>
#include <uapi/kernel/stat.h>

namespace kernel::vfs {

// Existing libc/fcntl.h flag values; no syscall or flag ABI changes.
constexpr int ACCESS_MODE = 3;
constexpr int APPEND = 0x0400;
constexpr int NONBLOCK = 0x0800;
constexpr int SETTABLE_FLAGS = APPEND | NONBLOCK;

static vfs_node* g_root = nullptr;

void vfs_manager::init() noexcept {
    kernel::print("VFS initialized\n");
}

vfs_node* vfs_manager::get_root() noexcept {
    return g_root;
}

void vfs_manager::set_root(vfs_node* node) noexcept {
    auto* previous = g_root;
    g_root = node;
    if (previous) previous->release();
}

namespace {
int validate_component(const char* name, size_t length) noexcept {
    if (!length) return -22;
    if (length > 127) return -36;
    // Parent traversal needs namespace/mount ancestry, which is not implemented.
    if ((length == 1 && name[0] == '.') || (length == 2 && name[0] == '.' && name[1] == '.')) return -95;
    return 0;
}
} // namespace

int vfs_manager::lookup(vfs_node* directory, const char* name, vfs_node*& result) noexcept {
    result = nullptr;
    if (!name) return -14;
    size_t length = 0;
    while (name[length]) {
        if (name[length] == '/') return -22;
        if (++length > 127) return -36;
    }
    const int validation = validate_component(name, length);
    if (validation < 0) return validation;
    if (!directory) return -2;
    if (directory->type != file_type::DIRECTORY) return -20;
    if (!directory->ops || !directory->ops->lookup) return -95;
    vfs_node* child = nullptr;
    const int error = directory->ops->lookup(directory, name, child);
    if (error < 0) return error;
    if (error || !child) return -5;
    result = child;
    return 0;
}

int vfs_manager::resolve_path(const char* path, vfs_node*& result) noexcept {
    result = nullptr;
    if (!path) return -14;
    if (!*path) return -2;
    if (*path != '/') return -22;
    // Validate the whole path before invoking a filesystem or allocating nodes.
    size_t length = 0;
    while (length < 1024 && path[length])
        ++length;
    if (length == 1024) return -36;
    for (size_t i = 1; i < length;) {
        if (path[i] == '/') {
            ++i;
            continue;
        }
        const size_t begin = i;
        while (i < length && path[i] != '/')
            ++i;
        const int error = validate_component(path + begin, i - begin);
        if (error < 0) return error;
    }
    if (!g_root) return -2;
    g_root->retain();
    node_ref current(g_root);
    size_t i = 0;
    while (i < length) {
        if (path[i] == '/') {
            if (current.get()->type != file_type::DIRECTORY) return -20;
            ++i;
            continue;
        }
        char component[128];
        size_t count = 0;
        while (i < length && path[i] != '/')
            component[count++] = path[i++];
        component[count] = '\0';
        vfs_node* child = nullptr;
        const int error = lookup(current.get(), component, child);
        if (error < 0) return error;
        current.reset(child);
    }
    result = current.detach();
    return 0;
}

// Helper: get fd entry, ensuring capacity exists. Returns nullptr on bad fd.
using fd_t = scheduler::thread::file_descriptor;
static fd_t* get_fd(scheduler::thread* t, int fd) {
    if (!t || !t->fd_table || fd < 0 || static_cast<size_t>(fd) >= t->fd_count) return nullptr;
    return &t->fd_table[fd];
}

int vfs_manager::alloc_fd(vfs_node* node) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -3;
    if (!node) return -22;
    // Socket/accept create duplex streams; sys_open replaces the default mode.
    const int flags = node->type == file_type::SOCKET ? 2 : 0;

    if (!t->fd_table) {
        if (t->fd_count || !t->ensure_fd_capacity(scheduler::thread::INITIAL_FDS)) return -12;
    }
    if (!t->fd_table) return -12;

    for (size_t i = 0; i < t->fd_count; ++i) {
        if (t->fd_table[i].node == nullptr) {
            node->retain();
            t->fd_table[i].node = node;
            t->fd_table[i].offset = 0;
            t->fd_table[i].flags = flags;
            return static_cast<int>(i);
        }
    }
    if (t->fd_count < scheduler::thread::MAX_FDS) {
        size_t old = t->fd_count;
        if (!t->ensure_fd_capacity(t->fd_count + 1)) return -12;
        if (old < t->fd_count) {
            node->retain();
            t->fd_table[old].node = node;
            t->fd_table[old].offset = 0;
            t->fd_table[old].flags = flags;
            return static_cast<int>(old);
        }
    }
    return t->fd_count >= scheduler::thread::MAX_FDS ? -24 : -12;
}

namespace {
void close_descriptor(fd_t& descriptor) noexcept {
    auto* node = static_cast<vfs_node*>(descriptor.node);
    descriptor = {}; // Detach before a callback can re-enter or destroy the node.
    if (node) node->close_reference();
}
} // namespace

void vfs_manager::free_fd(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (fdesc) close_descriptor(*fdesc);
}

void vfs_manager::close_all(scheduler::thread* owner) noexcept {
    if (!owner || !owner->fd_table) return;
    for (size_t i = 0; i < owner->fd_count; ++i)
        close_descriptor(owner->fd_table[i]);
}

int vfs_manager::copy_fds(const scheduler::thread* source, scheduler::thread* target) noexcept {
    if (!source || !target || source == target) return -22;
    // Clone supplies an empty destination; never replace live descriptors.
    if (target->fd_table) {
        for (size_t i = 0; i < target->fd_count; ++i)
            if (target->fd_table[i].node) return -22;
    }
    if (!source->fd_table || !source->fd_count) return 0;
    if (!target->ensure_fd_capacity(source->fd_count)) return -12;
    for (size_t i = 0; i < source->fd_count; ++i) {
        target->fd_table[i] = source->fd_table[i];
        auto* node = static_cast<vfs_node*>(target->fd_table[i].node);
        if (node) node->open_reference();
    }
    return 0;
}

vfs_node* vfs_manager::get_fd_node(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    return fdesc ? static_cast<vfs_node*>(fdesc->node) : nullptr;
}

int vfs_manager::sys_open(const char* path, int flags) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    if (!t) return -3;
    if ((flags & ACCESS_MODE) == ACCESS_MODE) return -22;
    vfs_node* node = nullptr;
    const int error = resolve_path(path, node);
    if (error < 0) return error;
    node_ref reference(node);
    constexpr int DIRECTORY = 00200000;
    if ((flags & DIRECTORY) && node->type != file_type::DIRECTORY) return -20;
    const int fd = alloc_fd(node);
    if (fd < 0) return fd;
    get_fd(t, fd)->flags = flags;
    if (node->ops && node->ops->open) node->ops->open(node);
    return fd;
}

long vfs_manager::sys_read(int fd, void* buffer, size_t size) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;
    const int access = fdesc->flags & ACCESS_MODE;
    if (access != 0 && access != 2) return -9;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (node->type == file_type::DIRECTORY) return -21;
    if (!size) return 0;
    if (!buffer) return -14;
    if (size > static_cast<size_t>(__LONG_MAX__)) return -75;
    if (!node->ops || !node->ops->read) return -22;

    const long bytes_read = node->ops->read(node, fdesc->offset, size, buffer);
    if (bytes_read > 0) {
        const auto count = static_cast<size_t>(bytes_read);
        if (count > size) return -5;
        if (count > SIZE_MAX - fdesc->offset) return -75;
        fdesc->offset += count;
    }
    return bytes_read;
}

long vfs_manager::sys_write(int fd, const void* buffer, size_t size) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;
    const int access = fdesc->flags & ACCESS_MODE;
    if (access != 1 && access != 2) return -9;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    if (node->type == file_type::DIRECTORY) return -21;
    if (!size) return 0;
    if (!buffer) return -14;
    if (size > static_cast<size_t>(__LONG_MAX__)) return -75;
    if (node->type == file_type::REGULAR && (fdesc->flags & APPEND)) {
        if (!node->ops || !node->ops->append) return -95;
        size_t end_offset = fdesc->offset;
        const long written = node->ops->append(node, size, buffer, end_offset);
        if (written > 0) fdesc->offset = end_offset;
        return written;
    }
    if (!node->ops || !node->ops->write) return -22;
    if (size > SIZE_MAX - fdesc->offset) return -75;

    const long bytes_written = node->ops->write(node, fdesc->offset, size, buffer);
    if (bytes_written > 0) {
        const auto count = static_cast<size_t>(bytes_written);
        if (count > size) return -5;
        fdesc->offset += count;
    }
    return bytes_written;
}

int vfs_manager::sys_close(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;
    close_descriptor(*fdesc);
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

static int refresh_metadata(vfs_node* node) noexcept {
    return node->ops && node->ops->refresh_metadata ? node->ops->refresh_metadata(node) : 0;
}

long vfs_manager::sys_lseek(int fd, long offset, int whence) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;

    vfs_node* node = static_cast<vfs_node*>(fdesc->node);
    size_t base = 0;
    if (whence == 1) {
        base = fdesc->offset;
    } else if (whence == 2) {
        const int result = refresh_metadata(node);
        if (result < 0) return result;
        base = node->length;
    } else if (whence != 0) {
        return -22;
    }
    size_t new_offset;
    if (offset < 0) {
        // Form the magnitude without negating LONG_MIN. A large EOF can still
        // yield a representable result when seeking backward (notably ARMv7).
        const auto distance = static_cast<size_t>(-(offset + 1)) + 1;
        if (distance > base) return -22;
        new_offset = base - distance;
    } else if (__builtin_add_overflow(base, static_cast<size_t>(offset), &new_offset)) {
        return -75;
    }
    if (new_offset > static_cast<size_t>(__LONG_MAX__)) return -75;
    fdesc->offset = new_offset;
    return static_cast<long>(new_offset);
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
    if (!path || !statbuf) return -14;
    vfs_node* node = nullptr;
    const int error = resolve_path(path, node);
    if (error < 0) return error;
    node_ref reference(node);
    const int result = refresh_metadata(node);
    if (result < 0) return result;
    fill_stat(node, statbuf);
    return 0;
}

int vfs_manager::sys_fstat(int fd, void* statbuf) noexcept {
    auto* node = get_fd_node(fd);
    if (!node) return -9;
    if (!statbuf) return -14;
    const int result = refresh_metadata(node);
    if (result < 0) return result;
    fill_stat(node, statbuf);
    return 0;
}

int vfs_manager::sys_ftruncate(int fd, long length) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;
    if (length < 0) return -22;
    // O_ACCMODE: 0=read-only, 1=write-only, 2=read/write.
    const int access = fdesc->flags & ACCESS_MODE;
    if (access != 1 && access != 2) return -22;
    auto* node = static_cast<vfs_node*>(fdesc->node);
    if (node->type == file_type::DIRECTORY) return -21;
    if (node->type != file_type::REGULAR) return -22;
    if (!node->ops || !node->ops->truncate) return -95;
    return node->ops->truncate(node, static_cast<size_t>(length));
}

int vfs_manager::sys_fsync(int fd) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;
    auto* node = static_cast<vfs_node*>(fdesc->node);
    if (!node->ops || !node->ops->fsync) return -22;
    return node->ops->fsync(node);
}

int vfs_manager::sys_fcntl(int fd, int cmd, long arg) noexcept {
    auto* t = scheduler::scheduler::current_thread();
    auto* fdesc = get_fd(t, fd);
    if (!fdesc || !fdesc->node) return -9;

    if (cmd == 3) return fdesc->flags & (ACCESS_MODE | SETTABLE_FLAGS); // F_GETFL
    if (cmd == 4) {
        fdesc->flags = (fdesc->flags & ~SETTABLE_FLAGS) | (static_cast<int>(arg) & SETTABLE_FLAGS);
        return 0;
    } // F_SETFL
    return -22;
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
        if (t->fatal_signal_pending()) return -4;
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
        if (t->fatal_signal_pending()) return -4;
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
    if (!node) return -12;
    node->type = file_type::EPOLL;

    // allocate data
    auto* data = new epoll_node_data();
    if (!data) {
        delete node;
        return -12;
    }
    data->head = nullptr;
    node->ptr = reinterpret_cast<vfs_node*>(data);

    // ops
    static vfs_ops epoll_ops = {};
    node->destroy = [](vfs_node* n) {
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

    node_ref reference(node);
    return alloc_fd(node);
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
        if (t && t->fatal_signal_pending()) return -4;
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
