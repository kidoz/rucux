// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel::console {

struct display_info {
    uint32_t columns;
    uint32_t rows;
    uint32_t width_pixels;
    uint32_t height_pixels;
    bool available;
};

struct sink {
    void (*putc)(char c) noexcept;
    display_info (*get_display_info)() noexcept;
};

void reset() noexcept;
bool register_sink(sink new_sink) noexcept;
void putc(char c) noexcept;
void write(const char* s) noexcept;

// Output serialization. kernel::print() holds the lock across a whole
// formatted line so concurrent CPUs cannot interleave mid-line.
uintptr_t lock_output() noexcept;
void unlock_output(uintptr_t flags) noexcept;
void putc_unlocked(char c) noexcept;
display_info get_display_info() noexcept;

} // namespace kernel::console
