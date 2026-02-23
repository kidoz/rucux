// SPDX-License-Identifier: MIT
#ifndef _ENDIAN_H
#define _ENDIAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#define PDP_ENDIAN 3412
#define BYTE_ORDER LITTLE_ENDIAN

// x86_64 is little endian
#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)

#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)

static inline uint16_t bswap16(uint16_t x) {
    return (uint16_t)(((x & 0x00FF) << 8) | ((x & 0xFF00) >> 8));
}

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000FF) << 24) | ((x & 0x0000FF00) << 8) | ((x & 0x00FF0000) >> 8) | ((x & 0xFF000000) >> 24);
}

static inline uint64_t bswap64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) | ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) | ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) | ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) | ((x & 0xFF00000000000000ULL) >> 56);
}

#define htobe16(x) bswap16(x)
#define htobe32(x) bswap32(x)
#define htobe64(x) bswap64(x)

#define be16toh(x) bswap16(x)
#define be32toh(x) bswap32(x)
#define be64toh(x) bswap64(x)

#ifdef __cplusplus
}
#endif

#endif // _ENDIAN_H