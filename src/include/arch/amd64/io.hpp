// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

inline void outb(uint16_t port, uint8_t val) noexcept {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

inline void outw(uint16_t port, uint16_t val) noexcept {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

inline void outl(uint16_t port, uint32_t val) noexcept {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

inline uint8_t inb(uint16_t port) noexcept {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline uint16_t inw(uint16_t port) noexcept {
    uint16_t ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline uint32_t inl(uint16_t port) noexcept {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

inline void inw_rep(uint16_t port, void* addr, uint32_t word_count) noexcept {
    asm volatile("cld; rep insw" : "+D"(addr), "+c"(word_count) : "d"(port) : "memory");
}

inline void outw_rep(uint16_t port, const void* addr, uint32_t word_count) noexcept {
    asm volatile("cld; rep outsw" : "+S"(addr), "+c"(word_count) : "d"(port) : "memory");
}

inline void io_wait() noexcept {
    outb(0x80, 0);
}

} // namespace arch::amd64
