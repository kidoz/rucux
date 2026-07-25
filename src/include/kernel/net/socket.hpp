// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::net {

// Kernel socket — wraps a TCP or UDP PCB
struct ksocket {
    int type;  // SOCK_STREAM or SOCK_DGRAM
    void* pcb; // tcp_pcb* or udp_pcb*
};

class socket_manager {
public:
    static void init() noexcept;

    static int sys_socket(int domain, int type, int protocol) noexcept;
    static int sys_bind(int sockfd, const void* addr, uint32_t addrlen) noexcept;
    static int sys_listen(int sockfd, int backlog) noexcept;
    static int sys_accept(int sockfd, void* addr, uint32_t* addrlen) noexcept;
    static int sys_connect(int sockfd, const void* addr, uint32_t addrlen) noexcept;
    static long sys_send(int sockfd, const void* buf, size_t len, int flags) noexcept;
    static long sys_recv(int sockfd, void* buf, size_t len, int flags) noexcept;
    static long sys_sendto(int sockfd, const void* buf, size_t len, int flags, const void* dest_addr,
                           uint32_t addrlen) noexcept;
    static long sys_recvfrom(int sockfd, void* buf, size_t len, int flags, void* src_addr, uint32_t* addrlen) noexcept;
    static int sys_setsockopt(int sockfd, int level, int optname, const void* optval, uint32_t optlen) noexcept;
    static int sys_getsockopt(int sockfd, int level, int optname, void* optval, uint32_t* optlen) noexcept;
    static int sys_getsockname(int sockfd, void* addr, uint32_t* addrlen) noexcept;
    static int sys_getpeername(int sockfd, void* addr, uint32_t* addrlen) noexcept;

    // For poll/select
    static int poll_socket(int sockfd) noexcept;
};

} // namespace kernel::net
