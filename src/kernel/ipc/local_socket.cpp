// SPDX-License-Identifier: MIT
#include <kernel/ipc/local_socket.hpp>

#include <kernel/print.hpp>
#include <kernel/vfs/vfs.hpp>
#include <lib/string.hpp>
#include <uapi/kernel/unix_ipc.h>

namespace kernel::ipc {
namespace {

static constexpr uint16_t AF_UNIX_K = 1;
static constexpr int POLLIN_K = 0x001;
static constexpr int POLLOUT_K = 0x004;
static constexpr int POLLHUP_K = 0x010;
static constexpr uint32_t LOCAL_SOCKET_POOL_SIZE = 64;
static constexpr size_t LOCAL_SOCKET_BUFFER_SIZE = 4096;

struct sockaddr_un_k {
    uint16_t sun_family;
    char sun_path[RUCUX_UNIX_IPC_PATH_MAX];
};

struct LocalSocket {
    bool in_use;
    uint32_t type;
    uint32_t refcount;
    bool listening;
    bool has_address;
    rucux_unix_ipc_address address;
    int peer_index;
    int pending_index;
    bool read_shutdown;
    bool write_shutdown;
    bool peer_closed;
    size_t rx_head;
    size_t rx_tail;
    size_t rx_size;
    char rx_buffer[LOCAL_SOCKET_BUFFER_SIZE];
    kernel::vfs::vfs_node vnode;
};

static LocalSocket g_local_sockets[LOCAL_SOCKET_POOL_SIZE] = {};

static size_t ring_write(LocalSocket& socket, const char* data, size_t len) noexcept {
    size_t capacity = LOCAL_SOCKET_BUFFER_SIZE - socket.rx_size;
    if (capacity == 0) {
        return 0;
    }
    if (len > capacity) {
        len = capacity;
    }

    size_t first = len;
    size_t contiguous = LOCAL_SOCKET_BUFFER_SIZE - socket.rx_tail;
    if (first > contiguous) {
        first = contiguous;
    }
    lib::memcpy(socket.rx_buffer + socket.rx_tail, data, first);
    if (len > first) {
        lib::memcpy(socket.rx_buffer, data + first, len - first);
    }
    socket.rx_tail = (socket.rx_tail + len) % LOCAL_SOCKET_BUFFER_SIZE;
    socket.rx_size += len;
    return len;
}

static size_t ring_read(LocalSocket& socket, char* data, size_t len) noexcept {
    if (len > socket.rx_size) {
        len = socket.rx_size;
    }

    size_t first = len;
    size_t contiguous = LOCAL_SOCKET_BUFFER_SIZE - socket.rx_head;
    if (first > contiguous) {
        first = contiguous;
    }
    lib::memcpy(data, socket.rx_buffer + socket.rx_head, first);
    if (len > first) {
        lib::memcpy(data + first, socket.rx_buffer, len - first);
    }
    socket.rx_head = (socket.rx_head + len) % LOCAL_SOCKET_BUFFER_SIZE;
    socket.rx_size -= len;
    return len;
}

static void reset_socket(LocalSocket& socket) noexcept {
    lib::memset(&socket, 0, sizeof(socket));
    socket.peer_index = -1;
    socket.pending_index = -1;
    socket.vnode.name = "unix";
    socket.vnode.name_hash = kernel::vfs::vfs_node::hash_name("unix");
    socket.vnode.type = kernel::vfs::file_type::CHAR_DEVICE;
    socket.vnode.length = 0;
    socket.vnode.uid = 0;
    socket.vnode.gid = 0;
    socket.vnode.mask = 0;
    socket.vnode.flags = 0;
    socket.vnode.ptr = reinterpret_cast<kernel::vfs::vfs_node*>(&socket);
}

static LocalSocket* socket_from_node(kernel::vfs::vfs_node* node) noexcept;

static void local_open(kernel::vfs::vfs_node* node) noexcept {
    auto* socket = socket_from_node(node);
    if (socket) {
        socket->refcount++;
    }
}

static void release_socket_index(int index) noexcept;

static void local_close(kernel::vfs::vfs_node* node) noexcept {
    auto* socket = socket_from_node(node);
    if (!socket || socket->refcount == 0) {
        return;
    }
    socket->refcount--;
    if (socket->refcount == 0) {
        release_socket_index(static_cast<int>(socket - g_local_sockets));
    }
}

static size_t local_read(kernel::vfs::vfs_node* node, size_t, size_t size, void* buffer) noexcept {
    auto* socket = socket_from_node(node);
    if (!socket || !buffer || socket->type != RUCUX_UNIX_IPC_SOCK_STREAM || socket->read_shutdown) {
        return 0;
    }
    if (socket->rx_size == 0) {
        return socket->peer_closed ? 0 : 0;
    }
    return ring_read(*socket, static_cast<char*>(buffer), size);
}

static size_t local_write(kernel::vfs::vfs_node* node, size_t, size_t size, const void* buffer) noexcept {
    auto* socket = socket_from_node(node);
    if (!socket || !buffer || socket->type != RUCUX_UNIX_IPC_SOCK_STREAM || socket->write_shutdown) {
        return 0;
    }
    if (socket->peer_index < 0) {
        return 0;
    }

    auto& peer = g_local_sockets[socket->peer_index];
    if (!peer.in_use || peer.read_shutdown) {
        return 0;
    }
    return ring_write(peer, static_cast<const char*>(buffer), size);
}

static int local_poll(kernel::vfs::vfs_node* node) noexcept {
    auto* socket = socket_from_node(node);
    if (!socket) {
        return 0;
    }

    int events = 0;
    if (socket->listening) {
        if (socket->pending_index >= 0) {
            events |= POLLIN_K;
        }
        return events;
    }

    if (socket->rx_size > 0 || socket->peer_closed) {
        events |= POLLIN_K;
    }
    if (!socket->write_shutdown && socket->peer_index >= 0) {
        auto& peer = g_local_sockets[socket->peer_index];
        if (peer.in_use && !peer.read_shutdown && peer.rx_size < LOCAL_SOCKET_BUFFER_SIZE) {
            events |= POLLOUT_K;
        }
    }
    if (socket->peer_closed && socket->rx_size == 0) {
        events |= POLLHUP_K;
    }
    return events;
}

static kernel::vfs::vfs_ops g_local_socket_ops = {
    .read = local_read,
    .write = local_write,
    .truncate = nullptr,
    .open = local_open,
    .close = local_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .mmap = nullptr,
    .fsync = nullptr,
    .poll = local_poll,
};

static LocalSocket* socket_from_node(kernel::vfs::vfs_node* node) noexcept {
    if (!node || node->ops != &g_local_socket_ops) {
        return nullptr;
    }
    auto* ptr = reinterpret_cast<LocalSocket*>(node->ptr);
    if (!ptr || !ptr->in_use) {
        return nullptr;
    }
    return ptr;
}

static LocalSocket* socket_from_fd(int fd) noexcept {
    return socket_from_node(kernel::vfs::vfs_manager::get_fd_node(fd));
}

static bool valid_socket_type(int type) noexcept {
    return type == static_cast<int>(RUCUX_UNIX_IPC_SOCK_STREAM) ||
           type == static_cast<int>(RUCUX_UNIX_IPC_SOCK_DGRAM) ||
           type == static_cast<int>(RUCUX_UNIX_IPC_SOCK_SEQPACKET);
}

static bool valid_address(const rucux_unix_ipc_address& address) noexcept {
    if (address.family != AF_UNIX_K) {
        return false;
    }
    if (address.namespace_kind != RUCUX_UNIX_IPC_NAMESPACE_FILESYSTEM &&
        address.namespace_kind != RUCUX_UNIX_IPC_NAMESPACE_ABSTRACT) {
        return false;
    }
    return address.path_length <= RUCUX_UNIX_IPC_PATH_MAX;
}

static bool address_equal(const rucux_unix_ipc_address& lhs, const rucux_unix_ipc_address& rhs) noexcept {
    if (lhs.family != rhs.family || lhs.namespace_kind != rhs.namespace_kind || lhs.path_length != rhs.path_length) {
        return false;
    }
    return lib::memcmp(lhs.path, rhs.path, lhs.path_length) == 0;
}

static void copy_address(rucux_unix_ipc_address& dst, const rucux_unix_ipc_address& src) noexcept {
    lib::memset(&dst, 0, sizeof(dst));
    dst.family = src.family;
    dst.namespace_kind = src.namespace_kind;
    dst.path_length = src.path_length;
    if (src.path_length > 0) {
        lib::memcpy(dst.path, src.path, src.path_length);
    }
}

static bool translate_address_in(const void* addr, uint32_t addrlen, rucux_unix_ipc_address& out) noexcept {
    if (!addr || addrlen < sizeof(uint16_t)) {
        return false;
    }

    const auto* un = reinterpret_cast<const sockaddr_un_k*>(addr);
    if (un->sun_family != AF_UNIX_K) {
        return false;
    }

    lib::memset(&out, 0, sizeof(out));
    out.family = AF_UNIX_K;

    uint32_t base = sizeof(uint16_t);
    if (addrlen < base) {
        return false;
    }
    uint32_t path_len = addrlen - base;
    if (path_len > RUCUX_UNIX_IPC_PATH_MAX) {
        path_len = RUCUX_UNIX_IPC_PATH_MAX;
    }

    if (path_len > 0 && un->sun_path[0] == '\0') {
        out.namespace_kind = RUCUX_UNIX_IPC_NAMESPACE_ABSTRACT;
        if (path_len > 1) {
            out.path_length = static_cast<uint16_t>(path_len - 1);
            lib::memcpy(out.path, un->sun_path + 1, out.path_length);
        }
        return true;
    }

    out.namespace_kind = RUCUX_UNIX_IPC_NAMESPACE_FILESYSTEM;
    while (out.path_length < path_len && un->sun_path[out.path_length] != '\0') {
        out.path_length++;
    }
    if (out.path_length == 0) {
        return false;
    }
    lib::memcpy(out.path, un->sun_path, out.path_length);
    return true;
}

static int translate_address_out(const rucux_unix_ipc_address* address, void* addr, uint32_t* addrlen) noexcept {
    if (!addrlen) {
        return -1;
    }
    uint32_t total_len = sizeof(uint16_t);
    if (address) {
        if (address->namespace_kind == RUCUX_UNIX_IPC_NAMESPACE_ABSTRACT) {
            if (static_cast<uint32_t>(address->path_length) + 1u > RUCUX_UNIX_IPC_PATH_MAX) {
                return -1;
            }
            total_len += address->path_length + 1;
        } else {
            if (static_cast<uint32_t>(address->path_length) + 1u > RUCUX_UNIX_IPC_PATH_MAX) {
                return -1;
            }
            total_len += address->path_length + 1;
        }
    }
    *addrlen = total_len;
    if (!addr) {
        return 0;
    }

    auto* un = reinterpret_cast<sockaddr_un_k*>(addr);
    lib::memset(un, 0, sizeof(*un));
    un->sun_family = AF_UNIX_K;
    if (address) {
        if (address->namespace_kind == RUCUX_UNIX_IPC_NAMESPACE_ABSTRACT) {
            if (address->path_length > 0) {
                lib::memcpy(un->sun_path + 1, address->path, address->path_length);
            }
        } else {
            if (address->path_length > 0) {
                lib::memcpy(un->sun_path, address->path, address->path_length);
            }
            un->sun_path[address->path_length] = '\0';
        }
    }
    return 0;
}

static int alloc_socket_slot() noexcept {
    for (uint32_t i = 0; i < LOCAL_SOCKET_POOL_SIZE; ++i) {
        if (!g_local_sockets[i].in_use) {
            reset_socket(g_local_sockets[i]);
            g_local_sockets[i].in_use = true;
            g_local_sockets[i].refcount = 1;
            g_local_sockets[i].vnode.ops = &g_local_socket_ops;
            return static_cast<int>(i);
        }
    }
    return -1;
}

static void release_socket_index(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(LOCAL_SOCKET_POOL_SIZE)) {
        return;
    }

    auto& socket = g_local_sockets[index];
    if (!socket.in_use) {
        return;
    }

    int peer_index = socket.peer_index;
    int pending_index = socket.pending_index;

    socket.in_use = false;
    socket.refcount = 0;

    if (peer_index >= 0 && peer_index < static_cast<int>(LOCAL_SOCKET_POOL_SIZE)) {
        auto& peer = g_local_sockets[peer_index];
        if (peer.in_use && peer.peer_index == index) {
            peer.peer_index = -1;
            peer.peer_closed = true;
        }
    }

    if (pending_index >= 0 && pending_index < static_cast<int>(LOCAL_SOCKET_POOL_SIZE)) {
        auto& pending = g_local_sockets[pending_index];
        if (pending.in_use && pending.refcount > 0) {
            pending.refcount = 1;
            release_socket_index(pending_index);
        }
    }

    reset_socket(socket);
}

static bool address_in_use(const rucux_unix_ipc_address& address) noexcept {
    for (uint32_t i = 0; i < LOCAL_SOCKET_POOL_SIZE; ++i) {
        auto& socket = g_local_sockets[i];
        if (!socket.in_use || !socket.has_address) {
            continue;
        }
        if (address_equal(socket.address, address)) {
            return true;
        }
    }
    return false;
}

static LocalSocket* find_listener(const rucux_unix_ipc_address& address, uint32_t type) noexcept {
    for (uint32_t i = 0; i < LOCAL_SOCKET_POOL_SIZE; ++i) {
        auto& socket = g_local_sockets[i];
        if (!socket.in_use || !socket.listening || !socket.has_address || socket.type != type) {
            continue;
        }
        if (address_equal(socket.address, address)) {
            return &socket;
        }
    }
    return nullptr;
}

static int install_new_socket_fd(int slot, int flags) noexcept {
    auto& socket = g_local_sockets[slot];
    int fd = kernel::vfs::vfs_manager::install_fd(&socket.vnode, flags);
    if (fd < 0) {
        release_socket_index(slot);
    }
    return fd;
}

static int create_unconnected_socket(int type, int flags) noexcept {
    if (!valid_socket_type(type)) {
        return -1;
    }
    int slot = alloc_socket_slot();
    if (slot < 0) {
        return -1;
    }
    g_local_sockets[slot].type = static_cast<uint32_t>(type);
    return install_new_socket_fd(slot, flags);
}

static int handle_socketpair(const rucux_unix_ipc_socket_request* request,
                             rucux_unix_ipc_pair_response* response) noexcept {
    if (!request || !response) return -1;
    uint32_t type = request->type & ~02000000; // Mask out SOCK_CLOEXEC
    if (type != RUCUX_UNIX_IPC_SOCK_STREAM) return -1;

    int first_slot = alloc_socket_slot();
    if (first_slot < 0) {
        return -1;
    }
    int second_slot = alloc_socket_slot();
    if (second_slot < 0) {
        release_socket_index(first_slot);
        return -1;
    }

    auto& first = g_local_sockets[first_slot];
    auto& second = g_local_sockets[second_slot];
    first.type = request->type;
    second.type = request->type;
    first.peer_index = second_slot;
    second.peer_index = first_slot;

    int first_fd = install_new_socket_fd(first_slot, 0);
    if (first_fd < 0) {
        release_socket_index(second_slot);
        return -1;
    }
    int second_fd = install_new_socket_fd(second_slot, 0);
    if (second_fd < 0) {
        kernel::vfs::vfs_manager::sys_close(first_fd);
        return -1;
    }

    response->first_fd = first_fd;
    response->second_fd = second_fd;
    return 0;
}

static int handle_shutdown(LocalSocket* socket, int how) noexcept {
    if (!socket) {
        return -1;
    }
    if (how == 0 || how == 2) {
        socket->read_shutdown = true;
    }
    if (how == 1 || how == 2) {
        socket->write_shutdown = true;
    }
    return 0;
}

} // namespace

int local_socket_manager::sys_socket(int type, int protocol) noexcept {
    if (protocol != 0) {
        return -1;
    }
    return create_unconnected_socket(type, 0);
}

int local_socket_manager::sys_bind(int sockfd, const void* addr, uint32_t addrlen) noexcept {
    kernel::print("sys_bind: fd={}, addrlen={}\n", sockfd, addrlen);
    auto* socket = socket_from_fd(sockfd);
    rucux_unix_ipc_address address = {};
    if (!socket || !translate_address_in(addr, static_cast<uint32_t>(addrlen), address) || !valid_address(address) ||
        socket->has_address) {
        kernel::print("sys_bind failed early\n");
        return -1;
    }
    if (address_in_use(address)) {
        kernel::print("sys_bind failed in use\n");
        return -1;
    }

    copy_address(socket->address, address);
    socket->has_address = true;
    kernel::print("sys_bind SUCCESS\n");
    return 0;
}

int local_socket_manager::sys_listen(int sockfd, int backlog) noexcept {
    kernel::print("sys_listen: fd={}\n", sockfd);
    auto* socket = socket_from_fd(sockfd);
    if (!socket || !socket->has_address || socket->type != RUCUX_UNIX_IPC_SOCK_STREAM) {
        kernel::print("sys_listen failed\n");
        return -1;
    }
    socket->listening = true;
    socket->vnode.flags = static_cast<uint16_t>(backlog < 0 ? 0 : backlog);
    kernel::print("sys_listen SUCCESS\n");
    return 0;
}

int local_socket_manager::sys_accept(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* listener = socket_from_fd(sockfd);
    if (!listener || !listener->listening || listener->pending_index < 0) {
        return -1;
    }

    int child_index = listener->pending_index;
    listener->pending_index = -1;

    auto& child = g_local_sockets[child_index];
    int fd = kernel::vfs::vfs_manager::install_fd(&child.vnode, 0);
    if (fd < 0) {
        release_socket_index(child_index);
        return -1;
    }

    if (addrlen) {
        if (translate_address_out(nullptr, addr, addrlen) < 0) {
            return -1;
        }
    }
    return fd;
}

int local_socket_manager::sys_connect(int sockfd, const void* addr, uint32_t addrlen) noexcept {
    auto* client = socket_from_fd(sockfd);
    rucux_unix_ipc_address address = {};
    if (!client || client->type != RUCUX_UNIX_IPC_SOCK_STREAM || client->peer_index >= 0 ||
        !translate_address_in(addr, addrlen, address) || !valid_address(address)) {
        kernel::print("sys_connect: failed validation fd={} addrlen={}\n", sockfd, addrlen);
        return -1;
    }

    auto* listener = find_listener(address, client->type);
    if (!listener) {
        kernel::print("sys_connect: no listener found\n");
        return -1;
    }
    if (listener->pending_index >= 0) {
        kernel::print("sys_connect: listener busy\n");
        return -1;
    }

    int server_slot = alloc_socket_slot();
    if (server_slot < 0) {
        return -1;
    }

    auto& server = g_local_sockets[server_slot];
    server.type = client->type;
    server.peer_index = static_cast<int>(client - g_local_sockets);
    server.has_address = listener->has_address;
    if (server.has_address) {
        copy_address(server.address, listener->address);
    }

    client->peer_index = server_slot;
    client->peer_closed = false;
    listener->pending_index = server_slot;
    return 0;
}

long local_socket_manager::sys_send(int sockfd, const void* buf, size_t len, int flags) noexcept {
    (void)flags;
    auto* socket = socket_from_fd(sockfd);
    if (!socket || !buf || socket->type != RUCUX_UNIX_IPC_SOCK_STREAM || socket->write_shutdown ||
        socket->peer_index < 0) {
        return -1;
    }

    auto& peer = g_local_sockets[socket->peer_index];
    if (!peer.in_use || peer.read_shutdown) {
        return -1;
    }

    size_t written = ring_write(peer, static_cast<const char*>(buf), len);
    if (written == 0 && len != 0) {
        return -1;
    }
    return static_cast<long>(written);
}

long local_socket_manager::sys_recv(int sockfd, void* buf, size_t len, int flags) noexcept {
    (void)flags;
    auto* socket = socket_from_fd(sockfd);
    if (!socket || !buf || socket->type != RUCUX_UNIX_IPC_SOCK_STREAM || socket->read_shutdown) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (socket->rx_size == 0) {
        return socket->peer_closed ? 0 : -1;
    }
    return static_cast<long>(ring_read(*socket, static_cast<char*>(buf), len));
}

int local_socket_manager::sys_shutdown(int sockfd, int how) noexcept {
    return handle_shutdown(socket_from_fd(sockfd), how);
}

int local_socket_manager::sys_setsockopt(int sockfd, int, int, const void*, uint32_t) noexcept {
    return handles_fd(sockfd) ? 0 : -1;
}

int local_socket_manager::sys_getsockopt(int sockfd, int, int, void* optval, uint32_t* optlen) noexcept {
    if (!handles_fd(sockfd)) {
        return -1;
    }
    if (optval && optlen && *optlen >= sizeof(int)) {
        *reinterpret_cast<int*>(optval) = 0;
        *optlen = sizeof(int);
    }
    return 0;
}

int local_socket_manager::sys_getsockname(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* socket = socket_from_fd(sockfd);
    if (!socket || !addrlen) {
        return -1;
    }
    return translate_address_out(socket->has_address ? &socket->address : nullptr, addr, addrlen);
}

int local_socket_manager::sys_getpeername(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* socket = socket_from_fd(sockfd);
    if (!socket || !addrlen) {
        return -1;
    }
    if (socket->peer_index < 0) {
        return -1;
    }
    auto& peer = g_local_sockets[socket->peer_index];
    return translate_address_out(peer.has_address ? &peer.address : nullptr, addr, addrlen);
}

int local_socket_manager::poll_fd(int sockfd) noexcept {
    auto* socket = socket_from_fd(sockfd);
    if (!socket) {
        return 0;
    }
    return local_poll(&socket->vnode);
}

bool local_socket_manager::handles_fd(int sockfd) noexcept {
    return socket_from_fd(sockfd) != nullptr;
}

long local_socket_manager::sys_unix_ipc(uint32_t op, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2, uintptr_t,
                                        uintptr_t) noexcept {
    switch (op) {
    case RUCUX_UNIX_IPC_OP_SOCKET: {
        auto* request = reinterpret_cast<const rucux_unix_ipc_socket_request*>(arg0);
        if (!request) {
            return -1;
        }
        return sys_socket(static_cast<int>(request->type), 0);
    }
    case RUCUX_UNIX_IPC_OP_BIND: {
        auto* socket = socket_from_fd(static_cast<int>(arg0));
        auto* address = reinterpret_cast<const rucux_unix_ipc_address*>(arg1);
        if (!socket || !address || socket->has_address || !valid_address(*address) || address_in_use(*address)) {
            return -1;
        }
        copy_address(socket->address, *address);
        socket->has_address = true;
        return 0;
    }
    case RUCUX_UNIX_IPC_OP_LISTEN: {
        auto* request = reinterpret_cast<const rucux_unix_ipc_listen_request*>(arg1);
        return request ? sys_listen(static_cast<int>(arg0), static_cast<int>(request->backlog)) : -1;
    }
    case RUCUX_UNIX_IPC_OP_ACCEPT:
        return sys_accept(static_cast<int>(arg0), reinterpret_cast<void*>(arg1), nullptr);
    case RUCUX_UNIX_IPC_OP_CONNECT: {
        auto* socket = socket_from_fd(static_cast<int>(arg0));
        auto* address = reinterpret_cast<const rucux_unix_ipc_address*>(arg1);
        if (!socket || !address || socket->type != RUCUX_UNIX_IPC_SOCK_STREAM || socket->peer_index >= 0 ||
            !valid_address(*address)) {
            return -1;
        }
        auto* listener = find_listener(*address, socket->type);
        if (!listener || listener->pending_index >= 0) {
            return -1;
        }
        int server_slot = alloc_socket_slot();
        if (server_slot < 0) {
            return -1;
        }
        auto& server = g_local_sockets[server_slot];
        server.type = socket->type;
        server.peer_index = static_cast<int>(socket - g_local_sockets);
        server.has_address = listener->has_address;
        if (server.has_address) {
            copy_address(server.address, listener->address);
        }
        socket->peer_index = server_slot;
        socket->peer_closed = false;
        listener->pending_index = server_slot;
        return 0;
    }
    case RUCUX_UNIX_IPC_OP_SOCKETPAIR:
        return handle_socketpair(reinterpret_cast<const rucux_unix_ipc_socket_request*>(arg0),
                                 reinterpret_cast<rucux_unix_ipc_pair_response*>(arg1));
    case RUCUX_UNIX_IPC_OP_SHUTDOWN: {
        auto* request = reinterpret_cast<const rucux_unix_ipc_shutdown_request*>(arg1);
        return request ? sys_shutdown(static_cast<int>(arg0), static_cast<int>(request->how)) : -1;
    }
    case RUCUX_UNIX_IPC_OP_SENDMSG:
        return sys_send(static_cast<int>(arg0), reinterpret_cast<const void*>(arg1), static_cast<size_t>(arg2), 0);
    case RUCUX_UNIX_IPC_OP_RECVMSG:
        return sys_recv(static_cast<int>(arg0), reinterpret_cast<void*>(arg1), static_cast<size_t>(arg2), 0);
    default:
        return -1;
    }
}

} // namespace kernel::ipc
