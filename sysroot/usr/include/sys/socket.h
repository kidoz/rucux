// SPDX-License-Identifier: MIT
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Domains
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10

// Types
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

// Protocols
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

typedef unsigned int socklen_t;
typedef unsigned int in_addr_t;

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct in_addr {
    unsigned int s_addr;
};

struct in6_addr {
    unsigned char s6_addr[16];
};

struct sockaddr_in {
    short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct sockaddr_in6 {
    short sin6_family;
    unsigned short sin6_port;
    unsigned int sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned int sin6_scope_id;
};

struct sockaddr_storage {
    short ss_family;
    char __ss_padding[128 - sizeof(short)];
};

#define PF_INET AF_INET
#define PF_INET6 AF_INET6

#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

#define INADDR_ANY 0
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_ERROR 3
#define SOMAXCONN 128

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr* addr, unsigned int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr* addr, unsigned int* addrlen);
int connect(int sockfd, const struct sockaddr* addr, unsigned int addrlen);
ssize_t send(int sockfd, const void* buf, size_t len, int flags);
ssize_t recv(int sockfd, void* buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen);
int shutdown(int sockfd, int how);
int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen);

// Utility functions
unsigned short htons(unsigned short hostshort);
unsigned int htonl(unsigned int hostlong);
unsigned short ntohs(unsigned short netshort);
unsigned int ntohl(unsigned int netlong);

#ifdef __cplusplus
}
#endif

#endif // _SYS_SOCKET_H
