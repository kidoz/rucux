// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/net/udp.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/vfs/vfs.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::net {

static constexpr int MAX_SOCKETS = 128;

static ksocket g_sockets[MAX_SOCKETS] = {};

static ksocket* get_socket(int sockfd) noexcept {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS) return nullptr;
    if (!g_sockets[sockfd].pcb) return nullptr;
    return &g_sockets[sockfd];
}

static int alloc_socket_idx() noexcept {
    for (int i = 0; i < MAX_SOCKETS; ++i)
        if (!g_sockets[i].pcb) return i;
    return -1;
}

static long read_socket(ksocket* ks, void* buffer, size_t size) noexcept {
    if (!ks) return -9;
    if (ks->type == 1) return tcp_recv(static_cast<tcp_pcb*>(ks->pcb), buffer, size);
    return udp_recvfrom(static_cast<udp_pcb*>(ks->pcb), buffer, size, nullptr, nullptr);
}
static long write_socket(ksocket* ks, const void* buffer, size_t size) noexcept {
    if (!ks) return -9;
    if (ks->type == 1) return tcp_send(static_cast<tcp_pcb*>(ks->pcb), buffer, size);
    auto* pcb = static_cast<udp_pcb*>(ks->pcb);
    return udp_sendto(pcb, buffer, size, pcb->remote_ip, pcb->remote_port);
}
static size_t socket_read(vfs::vfs_node* node, size_t, size_t size, void* buffer) {
    return static_cast<size_t>(read_socket(get_socket(node->inode), buffer, size));
}
static size_t socket_write(vfs::vfs_node* node, size_t, size_t size, const void* buffer) {
    return static_cast<size_t>(write_socket(get_socket(node->inode), buffer, size));
}

static void socket_close(vfs::vfs_node* node) {
    auto& socket = g_sockets[node->inode];
    if (socket.type == 2)
        udp_free(static_cast<udp_pcb*>(socket.pcb));
    else
        tcp_close(static_cast<tcp_pcb*>(socket.pcb));
    socket.pcb = nullptr;
    delete node;
}

static int socket_poll(vfs::vfs_node* node) {
    return socket_manager::poll_socket(node->inode);
}

static vfs::vfs_ops socket_ops = {
    .read = socket_read,
    .write = socket_write,
    .open = nullptr,
    .close = socket_close,
    .ioctl = nullptr,
    .readdir = nullptr,
    .finddir = nullptr,
    .mmap = nullptr,
    .poll = socket_poll,
};

struct sockaddr_in_k {
    short sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char sin_zero[8];
};

void socket_manager::init() noexcept {
    lib::memset(g_sockets, 0, sizeof(g_sockets));
    tcp_init();
    udp_init();
}

int socket_manager::sys_socket(int domain, int type, int protocol) noexcept {
    if (domain != 2) return -97; // AF_INET only
    if ((type != 1 && type != 2) || (protocol && protocol != (type == 1 ? 6 : 17))) return -93;
    int sidx = alloc_socket_idx();
    if (sidx < 0) return -1;

    auto& ks = g_sockets[sidx];
    ks.type = type;
    if (type == 1)
        ks.pcb = tcp_new();
    else if (type == 2)
        ks.pcb = udp_new();
    else
        return -1;

    if (!ks.pcb) return -1;

    auto* node = new vfs::vfs_node();
    if (!node) {
        if (type == 1)
            tcp_free(static_cast<tcp_pcb*>(ks.pcb));
        else
            udp_free(static_cast<udp_pcb*>(ks.pcb));
        ks.pcb = nullptr;
        return -12;
    }
    lib::memset(node, 0, sizeof(*node));
    node->type = vfs::file_type::SOCKET;
    node->inode = sidx;
    node->ops = &socket_ops;

    int fd = vfs::vfs_manager::alloc_fd(node);
    if (fd < 0) {
        socket_close(node);
        return -1;
    }
    return fd;
}

int socket_manager::sys_bind(int sockfd, const void* addr, uint32_t addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks) return -1;
    if (!addr || addrlen < sizeof(sockaddr_in_k)) return -22;
    auto* sa = reinterpret_cast<const sockaddr_in_k*>(addr);
    if (sa->sin_family != 2) return -97;
    if (ks->type == 1)
        return tcp_bind(static_cast<tcp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
    else
        return udp_bind(static_cast<udp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
}

int socket_manager::sys_listen(int sockfd, int backlog) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks || ks->type != 1) return -1;
    return tcp_listen(static_cast<tcp_pcb*>(ks->pcb), backlog);
}

int socket_manager::sys_accept(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks || ks->type != 1) return -1;
    auto* child = tcp_accept(static_cast<tcp_pcb*>(ks->pcb));
    if (!child) return -1;

    int nsidx = alloc_socket_idx();
    if (nsidx < 0) {
        tcp_close(child);
        return -1;
    }
    g_sockets[nsidx] = {1, child};

    auto* new_node = new vfs::vfs_node();
    if (!new_node) {
        tcp_close(child);
        g_sockets[nsidx].pcb = nullptr;
        return -12;
    }
    lib::memset(new_node, 0, sizeof(*new_node));
    new_node->type = vfs::file_type::SOCKET;
    new_node->inode = nsidx;
    new_node->ops = &socket_ops;

    int nfd = vfs::vfs_manager::alloc_fd(new_node);
    if (nfd < 0) {
        delete new_node;
        tcp_close(child);
        g_sockets[nsidx].pcb = nullptr;
        return -1;
    }

    if (addr && addrlen) {
        auto* sa = reinterpret_cast<sockaddr_in_k*>(addr);
        lib::memset(sa, 0, sizeof(*sa));
        sa->sin_family = 2;
        sa->sin_port = child->remote_port;
        sa->sin_addr = child->remote_ip;
        *addrlen = sizeof(sockaddr_in_k);
    }
    return nfd;
}

int socket_manager::sys_connect(int sockfd, const void* addr, uint32_t addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks) return -1;
    if (!addr || addrlen < sizeof(sockaddr_in_k)) return -22;
    auto* sa = reinterpret_cast<const sockaddr_in_k*>(addr);
    if (sa->sin_family != 2) return -97;
    if (ks->type == 1)
        return tcp_connect(static_cast<tcp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
    else
        return udp_connect(static_cast<udp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
}

long socket_manager::sys_send(int sockfd, const void* buf, size_t len, int flags) noexcept {
    if (flags) return -95;
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -9;
    return write_socket(get_socket(node->inode), buf, len);
}

long socket_manager::sys_recv(int sockfd, void* buf, size_t len, int flags) noexcept {
    if (flags) return -95;
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -9;
    return read_socket(get_socket(node->inode), buf, len);
}

long socket_manager::sys_sendto(int sockfd, const void* buf, size_t len, int, const void* dest_addr,
                                uint32_t addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks || ks->type != 2) return -1;
    if (!dest_addr || addrlen < sizeof(sockaddr_in_k)) return -22;
    auto* sa = reinterpret_cast<const sockaddr_in_k*>(dest_addr);
    if (sa->sin_family != 2) return -97;
    return udp_sendto(static_cast<udp_pcb*>(ks->pcb), buf, len, sa->sin_addr, sa->sin_port);
}

long socket_manager::sys_recvfrom(int sockfd, void* buf, size_t len, int, void* src_addr, uint32_t* addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks || ks->type != 2) return -1;
    uint32_t sip = 0;
    uint16_t sp = 0;
    long r = udp_recvfrom(static_cast<udp_pcb*>(ks->pcb), buf, len, &sip, &sp);
    if (r >= 0 && src_addr && addrlen) {
        auto* sa = reinterpret_cast<sockaddr_in_k*>(src_addr);
        lib::memset(sa, 0, sizeof(*sa));
        sa->sin_family = 2;
        sa->sin_port = sp;
        sa->sin_addr = sip;
        *addrlen = sizeof(sockaddr_in_k);
    }
    return r;
}

int socket_manager::sys_setsockopt(int, int, int, const void*, uint32_t) noexcept {
    return 0;
}
int socket_manager::sys_getsockopt(int, int, int, void* v, uint32_t* l) noexcept {
    if (v && l && *l >= 4) {
        *reinterpret_cast<int*>(v) = 0;
        *l = 4;
    }
    return 0;
}

int socket_manager::sys_getsockname(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks) return -1;
    auto* sa = reinterpret_cast<sockaddr_in_k*>(addr);
    sa->sin_family = 2;
    if (ks->type == 1) {
        auto* p = static_cast<tcp_pcb*>(ks->pcb);
        sa->sin_port = p->local_port;
        sa->sin_addr = p->local_ip;
    } else {
        auto* p = static_cast<udp_pcb*>(ks->pcb);
        sa->sin_port = p->local_port;
        sa->sin_addr = p->local_ip;
    }
    *addrlen = sizeof(sockaddr_in_k);
    return 0;
}

int socket_manager::sys_getpeername(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* node = vfs::vfs_manager::get_fd_node(sockfd);
    if (!node || node->ops != &socket_ops) return -1;
    auto* ks = get_socket(node->inode);
    if (!ks) return -1;
    auto* sa = reinterpret_cast<sockaddr_in_k*>(addr);
    sa->sin_family = 2;
    if (ks->type == 1) {
        auto* p = static_cast<tcp_pcb*>(ks->pcb);
        sa->sin_port = p->remote_port;
        sa->sin_addr = p->remote_ip;
    } else {
        auto* p = static_cast<udp_pcb*>(ks->pcb);
        sa->sin_port = p->remote_port;
        sa->sin_addr = p->remote_ip;
    }
    *addrlen = sizeof(sockaddr_in_k);
    return 0;
}

int socket_manager::poll_socket(int sockfd) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return 0;
    if (ks->type == 1) return tcp_poll_events(static_cast<tcp_pcb*>(ks->pcb));
    return udp_poll_events(static_cast<udp_pcb*>(ks->pcb));
}

} // namespace kernel::net
