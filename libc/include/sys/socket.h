// SPDX-License-Identifier: MIT
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Types
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

// Protocols
#define IPPROTO_IP  0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

typedef unsigned int socklen_t;
typedef unsigned int in_addr_t;
typedef unsigned short sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
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

#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_LOCAL  1
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX   AF_UNIX
#define PF_LOCAL  AF_LOCAL
#define AF_INET  2
#define AF_INET6 10
#define PF_INET AF_INET
#define PF_INET6 AF_INET6

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#define INADDR_ANY 0
extern const struct in6_addr in6addr_any;
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_ERROR 3
#define SO_KEEPALIVE 9
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_DONTROUTE 10
#define SOMAXCONN 128

#define IP_TOS 1
#define IPV6_TCLASS 67
#define IPV6_V6ONLY 26
#define IPPROTO_IPV6 41

#define MSG_OOB       0x01
#define MSG_PEEK      0x02
#define MSG_DONTROUTE 0x04

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int sockfd, const struct sockaddr *addr, unsigned int addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, unsigned int *addrlen);
int connect(int sockfd, const struct sockaddr *addr, unsigned int addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif // _SYS_SOCKET_H
