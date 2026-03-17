// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <string.h>
#include <stdint.h>

extern "C" {

uint16_t htons(uint16_t hostshort) {
    return (uint16_t)((hostshort >> 8) | (hostshort << 8));
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong >> 24) & 0xFF) | ((hostlong >> 8) & 0xFF00) | ((hostlong << 8) & 0xFF0000) |
           ((hostlong << 24) & 0xFF000000);
}

uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}

uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

char* inet_ntoa(struct in_addr in) {
    // static char buf[16];
    // unsigned char *bytes = (unsigned char *)&in.s_addr;
    (void)in;
    return (char*)"0.0.0.0";
}

int inet_pton(int af, const char* src, void* dst) {
    (void)af; (void)src; (void)dst;
    return 0;
}

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
    (void)af; (void)src; (void)dst; (void)size;
    return nullptr;
}

}
