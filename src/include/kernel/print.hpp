// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/console.hpp>
#include <lib/type_traits.hpp>
#include <stdint.h>

namespace kernel {

// Kernel console output routed to registered backends
void kputc(char c) noexcept;
void kwrite(const char* s) noexcept;

namespace detail {

template <typename T>
void print_int(T val, int base = 10) noexcept {
    static constexpr char digits[] = "0123456789ABCDEF";
    char buf[64];
    int i = 0;

    if (val == 0) {
        kputc('0');
        return;
    }

    bool negative = false;
    uint64_t uval;
    if constexpr (lib::is_same_v<T, int64_t> || lib::is_same_v<T, int32_t> || lib::is_same_v<T, int>) {
        if (val < 0) {
            negative = true;
            uval = -static_cast<int64_t>(val);
        } else {
            uval = static_cast<uint64_t>(val);
        }
    } else {
        uval = static_cast<uint64_t>(val);
    }

    while (uval > 0) {
        buf[i++] = digits[uval % base];
        uval /= base;
    }

    if (negative) kputc('-');

    while (i > 0) {
        kputc(buf[--i]);
    }
}

inline void print_arg(const char* s) noexcept {
    kwrite(s);
}
inline void print_arg(char c) noexcept {
    kputc(c);
}
inline void print_arg(bool b) noexcept {
    kwrite(b ? "true" : "false");
}
inline void print_arg(int val) noexcept {
    print_int(val);
}
inline void print_arg(unsigned int val) noexcept {
    print_int(val);
}
inline void print_arg(long val) noexcept {
    print_int(val);
}
inline void print_arg(unsigned long val) noexcept {
    print_int(val);
}
inline void print_arg(long long val) noexcept {
    print_int(val);
}
inline void print_arg(unsigned long long val) noexcept {
    print_int(val);
}
inline void print_arg(void* p) noexcept {
    kwrite("0x");
    print_int(reinterpret_cast<uintptr_t>(p), 16);
}

inline void print_impl(const char* fmt) noexcept {
    kwrite(fmt);
}

template <typename T>
concept Printable = requires(T a) { print_arg(a); };

template <Printable T, typename... Rest>
void print_impl(const char* fmt, T first, Rest... rest) noexcept {
    for (; *fmt != '\0'; ++fmt) {
        if (*fmt == '{' && *(fmt + 1) == '}') {
            print_arg(first);
            print_impl(fmt + 2, rest...);
            return;
        }
        kputc(*fmt);
    }
}

} // namespace detail

// Held for the duration of one print() so a formatted line is emitted whole.
// Locking inside kputc would only make single characters atomic, which still
// lets concurrent CPUs interleave mid-line.
template <typename... Args>
void print(const char* fmt, Args... args) noexcept {
    uintptr_t flags = console::lock_output();
    detail::print_impl(fmt, args...);
    console::unlock_output(flags);
}

} // namespace kernel
