// SPDX-License-Identifier: MIT
#ifndef _NETDB_H
#define _NETDB_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr* ai_addr;
    char* ai_canonname;
    struct addrinfo* ai_next;
};

#define AI_PASSIVE 1
#define AI_NUMERICHOST 4
#define AI_ADDRCONFIG 32
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2

struct hostent {
    char* h_name;
    char** h_aliases;
    int h_addrtype;
    int h_length;
    char** h_addr_list;
};

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
void freeaddrinfo(struct addrinfo* res);
const char* gai_strerror(int errcode);
int getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen, char* serv,
                socklen_t servlen, int flags);
struct hostent* gethostbyname(const char* name);

#ifdef __cplusplus
}
#endif

#endif // _NETDB_H