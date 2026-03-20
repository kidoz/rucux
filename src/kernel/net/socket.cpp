// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/net/udp.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <lib/string.hpp>

namespace kernel::net {

static constexpr int MAX_SOCKETS = 128;
static constexpr int SOCKET_FD_BASE = 100;

static ksocket g_sockets[MAX_SOCKETS] = {};

static ksocket* get_socket(int sockfd) noexcept {
    int idx = sockfd - SOCKET_FD_BASE;
    if (idx < 0 || idx >= MAX_SOCKETS) return nullptr;
    if (!g_sockets[idx].pcb) return nullptr;
    return &g_sockets[idx];
}

static int alloc_socket_fd() noexcept {
    for (int i = 0; i < MAX_SOCKETS; ++i)
        if (!g_sockets[i].pcb) return i + SOCKET_FD_BASE;
    return -1;
}

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

int socket_manager::sys_socket(int domain, int type, int) noexcept {
    if (domain != 2) return -1; // AF_INET only
    int fd = alloc_socket_fd();
    if (fd < 0) return -1;

    auto& ks = g_sockets[fd - SOCKET_FD_BASE];
    ks.type = type;
    if (type == 1)      ks.pcb = tcp_new();
    else if (type == 2) ks.pcb = udp_new();
    else return -1;
    if (!ks.pcb) return -1;
    return fd;
}

int socket_manager::sys_bind(int sockfd, const void* addr, uint32_t) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return -1;
    auto* sa = reinterpret_cast<const sockaddr_in_k*>(addr);
    if (ks->type == 1) return tcp_bind(static_cast<tcp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
    else               return udp_bind(static_cast<udp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
}

int socket_manager::sys_listen(int sockfd, int backlog) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks || ks->type != 1) return -1;
    return tcp_listen(static_cast<tcp_pcb*>(ks->pcb), backlog);
}

int socket_manager::sys_accept(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks || ks->type != 1) return -1;
    auto* child = tcp_accept(static_cast<tcp_pcb*>(ks->pcb));
    if (!child) return -1;
    int nfd = alloc_socket_fd();
    if (nfd < 0) { tcp_free(child); return -1; }
    g_sockets[nfd - SOCKET_FD_BASE] = {1, child};
    if (addr && addrlen) {
        auto* sa = reinterpret_cast<sockaddr_in_k*>(addr);
        sa->sin_family = 2; sa->sin_port = child->remote_port; sa->sin_addr = child->remote_ip;
        *addrlen = sizeof(sockaddr_in_k);
    }
    return nfd;
}

int socket_manager::sys_connect(int sockfd, const void* addr, uint32_t) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return -1;
    auto* sa = reinterpret_cast<const sockaddr_in_k*>(addr);
    if (ks->type == 1) return tcp_connect(static_cast<tcp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
    else               return udp_connect(static_cast<udp_pcb*>(ks->pcb), sa->sin_addr, sa->sin_port);
}

long socket_manager::sys_send(int sockfd, const void* buf, size_t len, int) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return -1;
    if (ks->type == 1) return tcp_send(static_cast<tcp_pcb*>(ks->pcb), buf, len);
    auto* p = static_cast<udp_pcb*>(ks->pcb);
    return udp_sendto(p, buf, len, p->remote_ip, p->remote_port);
}

long socket_manager::sys_recv(int sockfd, void* buf, size_t len, int) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return -1;
    if (ks->type == 1) return tcp_recv(static_cast<tcp_pcb*>(ks->pcb), buf, len);
    return udp_recvfrom(static_cast<udp_pcb*>(ks->pcb), buf, len, nullptr, nullptr);
}

long socket_manager::sys_sendto(int sockfd, const void* buf, size_t len, int,
                                 const void* dest_addr, uint32_t) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks || ks->type != 2) return -1;
    auto* sa = reinterpret_cast<const sockaddr_in_k*>(dest_addr);
    return udp_sendto(static_cast<udp_pcb*>(ks->pcb), buf, len, sa->sin_addr, sa->sin_port);
}

long socket_manager::sys_recvfrom(int sockfd, void* buf, size_t len, int,
                                   void* src_addr, uint32_t* addrlen) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks || ks->type != 2) return -1;
    uint32_t sip = 0; uint16_t sp = 0;
    long r = udp_recvfrom(static_cast<udp_pcb*>(ks->pcb), buf, len, &sip, &sp);
    if (r > 0 && src_addr && addrlen) {
        auto* sa = reinterpret_cast<sockaddr_in_k*>(src_addr);
        sa->sin_family = 2; sa->sin_port = sp; sa->sin_addr = sip;
        *addrlen = sizeof(sockaddr_in_k);
    }
    return r;
}

int socket_manager::sys_setsockopt(int, int, int, const void*, uint32_t) noexcept { return 0; }
int socket_manager::sys_getsockopt(int, int, int, void* v, uint32_t* l) noexcept {
    if (v && l && *l >= 4) { *reinterpret_cast<int*>(v) = 0; *l = 4; }
    return 0;
}

int socket_manager::sys_getsockname(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return -1;
    auto* sa = reinterpret_cast<sockaddr_in_k*>(addr);
    sa->sin_family = 2;
    if (ks->type == 1) { auto* p = static_cast<tcp_pcb*>(ks->pcb); sa->sin_port = p->local_port; sa->sin_addr = p->local_ip; }
    else { auto* p = static_cast<udp_pcb*>(ks->pcb); sa->sin_port = p->local_port; sa->sin_addr = p->local_ip; }
    *addrlen = sizeof(sockaddr_in_k);
    return 0;
}

int socket_manager::sys_getpeername(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    auto* ks = get_socket(sockfd);
    if (!ks) return -1;
    auto* sa = reinterpret_cast<sockaddr_in_k*>(addr);
    sa->sin_family = 2;
    if (ks->type == 1) { auto* p = static_cast<tcp_pcb*>(ks->pcb); sa->sin_port = p->remote_port; sa->sin_addr = p->remote_ip; }
    else { auto* p = static_cast<udp_pcb*>(ks->pcb); sa->sin_port = p->remote_port; sa->sin_addr = p->remote_ip; }
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
