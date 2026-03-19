// SPDX-License-Identifier: MIT
#include <string.h>
#include <sys/socket.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

const struct in6_addr in6addr_any = {{0}};

static long __syscall(long num, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}

int socket(int domain, int type, int protocol) {
    return (int)__syscall(SYS_SOCKET, (long)domain, (long)type, (long)protocol);
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)domain; (void)type; (void)protocol;
    if (sv) { sv[0] = -1; sv[1] = -1; }
    return -1; // stub
}

int bind(int sockfd, const struct sockaddr* addr, unsigned int addrlen) {
    return (int)__syscall(SYS_BIND, (long)sockfd, (long)addr, (long)addrlen);
}

int listen(int sockfd, int backlog) {
    return (int)__syscall(SYS_LISTEN, (long)sockfd, (long)backlog);
}

int accept(int sockfd, struct sockaddr* addr, unsigned int* addrlen) {
    return (int)__syscall(SYS_ACCEPT, (long)sockfd, (long)addr, (long)addrlen);
}

int connect(int sockfd, const struct sockaddr* addr, unsigned int addrlen) {
    return (int)__syscall(SYS_CONNECT, (long)sockfd, (long)addr, (long)addrlen);
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    return (ssize_t)__syscall(SYS_SEND, (long)sockfd, (long)buf, (long)len, (long)flags);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    return (ssize_t)__syscall(SYS_RECV, (long)sockfd, (long)buf, (long)len, (long)flags);
}

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr,
               socklen_t addrlen) {
    return (ssize_t)__syscall(SYS_SENDTO, (long)sockfd, (long)buf, (long)len, (long)flags, (long)dest_addr, (long)addrlen);
}

ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    return (ssize_t)__syscall(SYS_RECVFROM, (long)sockfd, (long)buf, (long)len, (long)flags, (long)src_addr, (long)addrlen);
}

int shutdown(int sockfd, int how) {
    (void)sockfd;
    (void)how;
    return 0; // stub
}

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
    return (int)__syscall(SYS_SETSOCKOPT, (long)sockfd, (long)level, (long)optname, (long)optval, (long)optlen);
}

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
    return (int)__syscall(SYS_GETSOCKOPT, (long)sockfd, (long)level, (long)optname, (long)optval, (long)optlen);
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)__syscall(SYS_GETSOCKNAME, (long)sockfd, (long)addr, (long)addrlen);
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)__syscall(SYS_GETPEERNAME, (long)sockfd, (long)addr, (long)addrlen);
}

} // extern "C"
