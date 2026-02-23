// SPDX-License-Identifier: MIT
#include <arch/amd64/framebuffer.hpp>
#include <kernel/memory/vmm.hpp>

namespace arch::amd64 {

static uint64_t g_fb_addr = 0;
static uint32_t g_fb_pitch = 0;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint8_t g_fb_bpp = 0;

void framebuffer::init(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch, uint8_t bpp) noexcept {
    g_fb_addr = addr;
    g_fb_width = width;
    g_fb_height = height;
    g_fb_pitch = pitch;
    g_fb_bpp = bpp;

    // We must map the framebuffer into virtual memory.
    // For simplicity, we identity map it.
    size_t size = pitch * height;
    for (size_t i = 0; i < size; i += 4096) {
        kernel::memory::vmm::map(addr + i, addr + i,
                                 kernel::memory::page_flags::PRESENT | kernel::memory::page_flags::WRITABLE |
                                     kernel::memory::page_flags::USER);
    }
}

void framebuffer::put_pixel(uint32_t x, uint32_t y, uint32_t color) noexcept {
    if (x >= g_fb_width || y >= g_fb_height) return;

    uint8_t* pixel = reinterpret_cast<uint8_t*>(g_fb_addr) + (y * g_fb_pitch) + (x * (g_fb_bpp / 8));
    if (g_fb_bpp == 32) {
        *reinterpret_cast<uint32_t*>(pixel) = color;
    } else if (g_fb_bpp == 24) {
        pixel[0] = color & 0xFF;
        pixel[1] = (color >> 8) & 0xFF;
        pixel[2] = (color >> 16) & 0xFF;
    }
}

void framebuffer::clear(uint32_t color) noexcept {
    fill_rect(0, 0, g_fb_width, g_fb_height, color);
}

void framebuffer::fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) noexcept {
    for (uint32_t cy = y; cy < y + h; ++cy) {
        for (uint32_t cx = x; cx < x + w; ++cx) {
            put_pixel(cx, cy, color);
        }
    }
}

uint32_t framebuffer::get_width() noexcept {
    return g_fb_width;
}
uint32_t framebuffer::get_height() noexcept {
    return g_fb_height;
}

} // namespace arch::amd64
