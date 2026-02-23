// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace arch::amd64 {

class framebuffer {
public:
    static void init(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) noexcept;
    static void put_pixel(uint32_t x, uint32_t y, uint32_t color) noexcept;
    static void clear(uint32_t color) noexcept;
    static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) noexcept;

    static uint32_t get_width() noexcept;
    static uint32_t get_height() noexcept;
};

} // namespace arch::amd64
