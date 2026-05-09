// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::ipc {

class local_socket_manager {
public:
    static int sys_socket(int type, int protocol) noexcept;
    static int sys_bind(int sockfd, const void* addr, uint32_t addrlen) noexcept;
    static int sys_listen(int sockfd, int backlog) noexcept;
    static int sys_accept(int sockfd, void* addr, uint32_t* addrlen) noexcept;
    static int sys_connect(int sockfd, const void* addr, uint32_t addrlen) noexcept;
    static long sys_send(int sockfd, const void* buf, size_t len, int flags) noexcept;
    static long sys_recv(int sockfd, void* buf, size_t len, int flags) noexcept;
    static int sys_shutdown(int sockfd, int how) noexcept;
    static int sys_setsockopt(int sockfd, int level, int optname, const void* optval,
                              uint32_t optlen) noexcept;
    static int sys_getsockopt(int sockfd, int level, int optname, void* optval,
                              uint32_t* optlen) noexcept;
    static int sys_getsockname(int sockfd, void* addr, uint32_t* addrlen) noexcept;
    static int sys_getpeername(int sockfd, void* addr, uint32_t* addrlen) noexcept;
    static int poll_fd(int sockfd) noexcept;
    static bool handles_fd(int sockfd) noexcept;
    static long sys_unix_ipc(uint32_t op, uintptr_t arg0, uintptr_t arg1, uintptr_t arg2,
                             uintptr_t arg3, uintptr_t arg4) noexcept;
};

} // namespace kernel::ipc
