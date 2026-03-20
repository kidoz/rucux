// SPDX-License-Identifier: MIT
#include <arpa/inet.h>
#include <string.h>
#include <stdint.h>

extern "C" {

uint16_t htons(uint16_t hostshort) {
    return (uint16_t)((hostshort >> 8) | (hostshort << 8));
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong >> 24) & 0xFF) | ((hostlong >> 8) & 0xFF00) |
           ((hostlong << 8) & 0xFF0000) | ((hostlong << 24) & 0xFF000000);
}

uint16_t ntohs(uint16_t netshort) { return htons(netshort); }
uint32_t ntohl(uint32_t netlong) { return htonl(netlong); }

char* inet_ntoa(struct in_addr in) {
    static char buf[16];
    unsigned char* b = (unsigned char*)&in.s_addr;
    char* p = buf;
    for (int i = 0; i < 4; ++i) {
        if (i) *p++ = '.';
        unsigned char v = b[i];
        if (v >= 100) { *p++ = '0' + v / 100; v %= 100; *p++ = '0' + v / 10; v %= 10; }
        else if (v >= 10) { *p++ = '0' + v / 10; v %= 10; }
        *p++ = '0' + v;
    }
    *p = '\0';
    return buf;
}

int inet_pton(int af, const char* src, void* dst) {
    if (af != 2 || !src || !dst) return -1;

    uint32_t addr = 0;
    int dots = 0;
    int val = 0;
    bool any = false;

    while (*src) {
        if (*src >= '0' && *src <= '9') {
            val = val * 10 + (*src - '0');
            if (val > 255) return 0;
            any = true;
        } else if (*src == '.') {
            if (!any || dots >= 3) return 0;
            addr = (addr << 8) | (uint32_t)val;
            val = 0;
            any = false;
            dots++;
        } else {
            return 0;
        }
        src++;
    }

    if (!any || dots != 3) return 0;
    addr = (addr << 8) | (uint32_t)val;
    *(uint32_t*)dst = htonl(addr);
    return 1;
}

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
    if (af != 2 || !src || !dst || size < 16) return nullptr;

    const unsigned char* b = (const unsigned char*)src;
    char* p = dst;
    for (int i = 0; i < 4; ++i) {
        if (i) *p++ = '.';
        unsigned char v = b[i];
        if (v >= 100) { *p++ = '0' + v / 100; v %= 100; *p++ = '0' + v / 10; v %= 10; }
        else if (v >= 10) { *p++ = '0' + v / 10; v %= 10; }
        *p++ = '0' + v;
    }
    *p = '\0';
    return dst;
}

} // extern "C"
