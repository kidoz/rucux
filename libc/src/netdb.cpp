// SPDX-License-Identifier: MIT
#include <netdb.h>

extern "C" {

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    (void)node;
    (void)service;
    (void)hints;
    (void)res;
    return -1; // stub
}

void freeaddrinfo(struct addrinfo* res) {
    (void)res;
}

const char* gai_strerror(int errcode) {
    (void)errcode;
    return "Unknown error"; // stub
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
