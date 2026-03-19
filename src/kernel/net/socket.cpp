// SPDX-License-Identifier: MIT
#include <kernel/net/socket.hpp>

namespace kernel::net {

int socket_manager::sys_socket(int domain, int type, int protocol) noexcept {
    (void)domain;
    (void)type;
    (void)protocol;
    return -1; // ENOSYS
}

int socket_manager::sys_bind(int sockfd, const void* addr, uint32_t addrlen) noexcept {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1; // ENOSYS
}

int socket_manager::sys_listen(int sockfd, int backlog) noexcept {
    (void)sockfd;
    (void)backlog;
    return -1; // ENOSYS
}

int socket_manager::sys_accept(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1; // ENOSYS
}

int socket_manager::sys_connect(int sockfd, const void* addr, uint32_t addrlen) noexcept {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1; // ENOSYS
}

long socket_manager::sys_send(int sockfd, const void* buf, size_t len, int flags) noexcept {
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    return -1; // ENOSYS
}

long socket_manager::sys_recv(int sockfd, void* buf, size_t len, int flags) noexcept {
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    return -1; // ENOSYS
}

long socket_manager::sys_sendto(int sockfd, const void* buf, size_t len, int flags, const void* dest_addr, uint32_t addrlen) noexcept {
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    (void)dest_addr;
    (void)addrlen;
    return -1; // ENOSYS
}

long socket_manager::sys_recvfrom(int sockfd, void* buf, size_t len, int flags, void* src_addr, uint32_t* addrlen) noexcept {
    (void)sockfd;
    (void)buf;
    (void)len;
    (void)flags;
    (void)src_addr;
    (void)addrlen;
    return -1; // ENOSYS
}

int socket_manager::sys_setsockopt(int sockfd, int level, int optname, const void* optval, uint32_t optlen) noexcept {
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return -1; // ENOSYS
}

int socket_manager::sys_getsockopt(int sockfd, int level, int optname, void* optval, uint32_t* optlen) noexcept {
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return -1; // ENOSYS
}

int socket_manager::sys_getsockname(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1; // ENOSYS
}

int socket_manager::sys_getpeername(int sockfd, void* addr, uint32_t* addrlen) noexcept {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1; // ENOSYS
}

} // namespace kernel::net