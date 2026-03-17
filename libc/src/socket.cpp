// SPDX-License-Identifier: MIT
#include <string.h>
#include <sys/socket.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>

extern "C" {

struct message {
    uint32_t sender;
    uint32_t type;
    uint64_t data[4];
};

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

static uint32_t g_net_tid = 0;

static uint32_t get_net_tid() {
    if (g_net_tid == 0) {
        message lookup;
        lookup.type = 11; // LOOKUP_SERVICE
        const char* name = "net";
        for (int i = 0; i < 8; ++i)
            reinterpret_cast<char*>(lookup.data)[i] = name[i];
        __syscall(SYS_IPC_SEND, 1, (long)&lookup);

        message resp;
        __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
        g_net_tid = static_cast<uint32_t>(resp.data[0]);
    }
    return g_net_tid;
}

int socket(int domain, int type, int protocol) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_SOCKET;
    m.data[0] = domain;
    m.data[1] = type;
    m.data[2] = protocol;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (int)resp.data[0];
}

int bind(int sockfd, const struct sockaddr* addr, unsigned int addrlen) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_BIND;
    m.data[0] = sockfd;
    m.data[1] = (uint64_t)addr;
    m.data[2] = addrlen;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (int)resp.data[0];
}

int listen(int sockfd, int backlog) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_LISTEN;
    m.data[0] = sockfd;
    m.data[1] = backlog;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (int)resp.data[0];
}

int accept(int sockfd, struct sockaddr* addr, unsigned int* addrlen) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_ACCEPT;
    m.data[0] = sockfd;
    m.data[1] = (uint64_t)addr;
    m.data[2] = (uint64_t)addrlen;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (int)resp.data[0];
}

int connect(int sockfd, const struct sockaddr* addr, unsigned int addrlen) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_CONNECT;
    m.data[0] = sockfd;
    m.data[1] = (uint64_t)addr;
    m.data[2] = addrlen;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (int)resp.data[0];
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_SEND;
    m.data[0] = sockfd;
    m.data[1] = (uint64_t)buf;
    m.data[2] = len;
    m.data[3] = flags;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (ssize_t)resp.data[0];
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    uint32_t tid = get_net_tid();
    if (!tid) return -1;

    message m;
    m.type = SYS_RECV;
    m.data[0] = sockfd;
    m.data[1] = (uint64_t)buf;
    m.data[2] = len;
    m.data[3] = flags;
    __syscall(SYS_IPC_SEND, tid, (long)&m);

    message resp;
    __syscall(SYS_IPC_RECV, (long)&resp, 0, 0);
    return (ssize_t)resp.data[0];
}

ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr,
               socklen_t addrlen) {
    (void)dest_addr;
    (void)addrlen;
    return send(sockfd, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    (void)src_addr;
    (void)addrlen;
    return recv(sockfd, buf, len, flags);
}

int shutdown(int sockfd, int how) {
    (void)sockfd;
    (void)how;
    return 0; // stub
}

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0; // stub
}

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0; // stub
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return 0; // stub
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    (void)sockfd; (void)addr; (void)addrlen;
    return 0; // stub
}

} // extern "C"
