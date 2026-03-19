// SPDX-License-Identifier: MIT
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>

extern "C" {

static int parse_ipv4(const char* str, uint32_t* ip) {
    int a, b, c, d;
    if (sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        *ip = (a << 24) | (b << 16) | (c << 8) | d;
        // Host to network byte order (little endian to big endian)
        *ip = __builtin_bswap32(*ip);
        return 1;
    }
    return 0;
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    if (!node && !service) return EAI_NONAME;

    struct addrinfo* info = (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
    if (!info) return EAI_MEMORY;

    struct sockaddr_in* sa = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
    if (!sa) {
        free(info);
        return EAI_MEMORY;
    }

    info->ai_family = AF_INET;
    info->ai_socktype = SOCK_STREAM;
    if (hints) {
        if (hints->ai_family != AF_UNSPEC) info->ai_family = hints->ai_family;
        if (hints->ai_socktype != 0) info->ai_socktype = hints->ai_socktype;
    }

    sa->sin_family = info->ai_family;
    
    if (service) {
        int port = atoi(service);
        sa->sin_port = __builtin_bswap16((uint16_t)port); // htons
    }

    if (node) {
        if (!parse_ipv4(node, &sa->sin_addr.s_addr)) {
            if (strcmp(node, "localhost") == 0) {
                sa->sin_addr.s_addr = __builtin_bswap32(0x7F000001);
            } else {
                free(info);
                free(sa);
                return EAI_NONAME; // Real DNS not implemented
            }
        }
    } else {
        sa->sin_addr.s_addr = __builtin_bswap32(INADDR_ANY);
    }

    info->ai_addr = (struct sockaddr*)sa;
    info->ai_addrlen = sizeof(struct sockaddr_in);
    info->ai_next = nullptr;
    
    *res = info;
    return 0;
}

void freeaddrinfo(struct addrinfo* res) {
    while (res) {
        struct addrinfo* next = res->ai_next;
        free(res->ai_addr);
        free(res->ai_canonname);
        free(res);
        res = next;
    }
}

const char* gai_strerror(int errcode) {
    switch (errcode) {
        case EAI_NONAME: return "Name or service not known";
        case EAI_MEMORY: return "Memory allocation failure";
        case EAI_FAMILY: return "Address family not supported";
        default: return "Unknown error";
    }
}

int getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen, char* serv,
                socklen_t servlen, int flags) {
    (void)sa;
    (void)salen;
    (void)host;
    (void)hostlen;
    (void)serv;
    (void)servlen;
    (void)flags;
    return -1; // stub
}

struct hostent* gethostbyname(const char* name) {
    (void)name;
    return nullptr; // stub
}

struct servent *getservbyname(const char *name, const char *proto) {
    (void)name; (void)proto;
    return nullptr; // stub
}

}
