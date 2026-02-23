// SPDX-License-Identifier: MIT
#include <lib/string.hpp>

namespace lib {

extern "C" {

void* memcpy(void* __restrict dest, const void* __restrict src, size_t n) noexcept {
    auto* d = static_cast<uint8_t*>(dest);
    const auto* s = static_cast<const uint8_t*>(src);

    // Basic word-based optimization
    if (n >= 16) {
        while (reinterpret_cast<uintptr_t>(d) % 8 != 0 && n > 0) {
            *d++ = *s++;
            n--;
        }
        auto* d_64 = reinterpret_cast<uint64_t*>(d);
        const auto* s_64 = reinterpret_cast<const uint64_t*>(s);
        while (n >= 8) {
            *d_64++ = *s_64++;
            n -= 8;
        }
        d = reinterpret_cast<uint8_t*>(d_64);
        s = reinterpret_cast<const uint8_t*>(s_64);
    }

    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* memset(void* s, int c, size_t n) noexcept {
    auto* p = static_cast<uint8_t*>(s);
    auto val = static_cast<uint8_t>(c);

    if (n >= 16) {
        uint64_t val_64 = (static_cast<uint64_t>(val) << 56) | (static_cast<uint64_t>(val) << 48) |
                          (static_cast<uint64_t>(val) << 40) | (static_cast<uint64_t>(val) << 32) |
                          (static_cast<uint64_t>(val) << 24) | (static_cast<uint64_t>(val) << 16) |
                          (static_cast<uint64_t>(val) << 8) | (static_cast<uint64_t>(val));

        while (reinterpret_cast<uintptr_t>(p) % 8 != 0 && n > 0) {
            *p++ = val;
            n--;
        }
        auto* p_64 = reinterpret_cast<uint64_t*>(p);
        while (n >= 8) {
            *p_64++ = val_64;
            n -= 8;
        }
        p = reinterpret_cast<uint8_t*>(p_64);
    }

    while (n--) {
        *p++ = val;
    }
    return s;
}

void* memmove(void* dest, const void* src, size_t n) noexcept {
    auto* d = static_cast<uint8_t*>(dest);
    const auto* s = static_cast<const uint8_t*>(src);

    if (d < s) {
        return memcpy(dest, src, n);
    }
    if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) noexcept {
    const auto* p1 = static_cast<const uint8_t*>(s1);
    const auto* p2 = static_cast<const uint8_t*>(s2);

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

char* strcpy(char* dest, const char* src) noexcept {
    char* d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

int strcmp(const char* s1, const char* s2) noexcept {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

} // extern "C"

} // namespace lib
