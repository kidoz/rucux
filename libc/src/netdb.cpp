// SPDX-License-Identifier: MIT
// DNS resolver: sends UDP queries to resolve hostnames via getaddrinfo.
// Supports A records (IPv4). Default DNS server: 8.8.8.8:53.
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {

static int parse_ipv4(const char* str, uint32_t* ip) {
    int a = 0, b = 0, c = 0, d = 0;
    int field = 0, val = 0;
    const char* p = str;
    while (*p) {
        if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); }
        else if (*p == '.') {
            if (field == 0) a = val; else if (field == 1) b = val; else if (field == 2) c = val;
            val = 0; field++;
        } else { return 0; }
        p++;
    }
    if (field != 3) return 0;
    d = val;
    if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
    *ip = __builtin_bswap32((uint32_t)((a << 24) | (b << 16) | (c << 8) | d));
    return 1;
}

// ─── DNS query/response ────────────────────────────────────────────────────

struct dns_header {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
} __attribute__((packed));

static int dns_encode_name(const char* name, uint8_t* out, int max_len) {
    int pos = 0;
    const char* p = name;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - p);
        if (pos + 1 + len >= max_len) return -1;
        out[pos++] = (uint8_t)len;
        for (int i = 0; i < len; ++i) out[pos++] = (uint8_t)p[i];
        p = (*dot == '.') ? dot + 1 : dot;
    }
    if (pos >= max_len) return -1;
    out[pos++] = 0;
    return pos;
}

static int dns_resolve(const char* hostname, uint32_t* out_ip) {
    uint8_t query[512];
    memset(query, 0, sizeof(query));

    dns_header* hdr = (dns_header*)query;
    hdr->id = __builtin_bswap16(0x1234);
    hdr->flags = __builtin_bswap16(0x0100); // RD=1
    hdr->qdcount = __builtin_bswap16(1);

    int nlen = dns_encode_name(hostname, query + sizeof(dns_header), 256);
    if (nlen < 0) return -1;

    int qpos = (int)sizeof(dns_header) + nlen;
    query[qpos++] = 0; query[qpos++] = 1; // QTYPE=A
    query[qpos++] = 0; query[qpos++] = 1; // QCLASS=IN

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = __builtin_bswap16(53);
    dns_addr.sin_addr.s_addr = __builtin_bswap32(0x08080808); // 8.8.8.8

    ssize_t sent = sendto(sock, query, (size_t)qpos, 0, (struct sockaddr*)&dns_addr, sizeof(dns_addr));
    if (sent < 0) { close(sock); return -1; }

    uint8_t resp[512];
    ssize_t rlen = recv(sock, resp, sizeof(resp), 0);
    close(sock);

    if (rlen < (ssize_t)(sizeof(dns_header) + nlen + 4)) return -1;

    dns_header* rh = (dns_header*)resp;
    int ancount = __builtin_bswap16(rh->ancount);
    if (ancount == 0) return -1;

    // Skip question section
    int rpos = (int)sizeof(dns_header) + nlen + 4;

    for (int i = 0; i < ancount && rpos < (int)rlen; ++i) {
        // Skip name (compressed pointer or label)
        if ((resp[rpos] & 0xC0) == 0xC0) rpos += 2;
        else { while (rpos < (int)rlen && resp[rpos] != 0) rpos += 1 + resp[rpos]; rpos++; }

        if (rpos + 10 > (int)rlen) break;
        uint16_t rtype = __builtin_bswap16(*(uint16_t*)(resp + rpos)); rpos += 2;
        rpos += 2; // class
        rpos += 4; // ttl
        uint16_t rdlen = __builtin_bswap16(*(uint16_t*)(resp + rpos)); rpos += 2;

        if (rtype == 1 && rdlen == 4 && rpos + 4 <= (int)rlen) {
            memcpy(out_ip, resp + rpos, 4);
            return 0;
        }
        rpos += rdlen;
    }
    return -1;
}

// ─── getaddrinfo ───────────────────────────────────────────────────────────

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    if (!node && !service) return EAI_NONAME;

    struct addrinfo* info = (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
    if (!info) return EAI_MEMORY;

    struct sockaddr_in* sa = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
    if (!sa) { free(info); return EAI_MEMORY; }

    info->ai_family = AF_INET;
    info->ai_socktype = SOCK_STREAM;
    if (hints) {
        if (hints->ai_family != AF_UNSPEC) info->ai_family = hints->ai_family;
        if (hints->ai_socktype != 0) info->ai_socktype = hints->ai_socktype;
    }

    sa->sin_family = (short)info->ai_family;
    if (service) sa->sin_port = __builtin_bswap16((uint16_t)atoi(service));

    if (node) {
        if (!parse_ipv4(node, &sa->sin_addr.s_addr)) {
            if (strcmp(node, "localhost") == 0) {
                sa->sin_addr.s_addr = __builtin_bswap32(0x7F000001);
            } else if (dns_resolve(node, &sa->sin_addr.s_addr) != 0) {
                free(info); free(sa);
                return EAI_NONAME;
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

int getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen,
                char* serv, socklen_t servlen, int flags) {
    (void)flags;
    if (!sa || salen < sizeof(struct sockaddr_in)) return -1;
    const struct sockaddr_in* sin = (const struct sockaddr_in*)sa;
    if (host && hostlen > 0) {
        const unsigned char* b = (const unsigned char*)&sin->sin_addr.s_addr;
        snprintf(host, (size_t)hostlen, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    }
    if (serv && servlen > 0)
        snprintf(serv, (size_t)servlen, "%d", __builtin_bswap16(sin->sin_port));
    return 0;
}

struct hostent* gethostbyname(const char* name) {
    static struct hostent h;
    static uint32_t addr;
    static char* addr_list[2] = {(char*)&addr, nullptr};

    if (parse_ipv4(name, &addr) || dns_resolve(name, &addr) == 0) {
        h.h_name = (char*)name;
        h.h_addrtype = AF_INET;
        h.h_length = 4;
        h.h_addr_list = addr_list;
        return &h;
    }
    return nullptr;
}

struct servent* getservbyname(const char*, const char*) { return nullptr; }

} // extern "C"
