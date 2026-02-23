// SPDX-License-Identifier: MIT
#include <kernel/memory/pmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

static uint8_t* g_bitmap;
static size_t g_bitmap_size;
static size_t g_total_pages;
static size_t g_used_pages;
static uintptr_t g_last_search_idx = 0;

void pmm::init(const memory_map_entry* entries, size_t entry_count) noexcept {
    uintptr_t max_addr = 0;
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].type == 1) { // Only count usable memory for max_addr
            uintptr_t end = entries[i].base + entries[i].length;
            if (end > max_addr) max_addr = end;
        }
    }

    g_total_pages = max_addr / PAGE_SIZE;
    g_bitmap_size = g_total_pages / 8;
    if (g_total_pages % 8 != 0) g_bitmap_size++;

    // Find a place for the bitmap in memory
    // In a real kernel, we'd find an USABLE entry large enough.
    // For now, let's assume we can find one.
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].type == 1 && entries[i].length >= g_bitmap_size) { // 1 = Usable
            g_bitmap = reinterpret_cast<uint8_t*>(entries[i].base);
            lib::memset(g_bitmap, 0xFF, g_bitmap_size); // Mark all as used initially
            break;
        }
    }

    if (!g_bitmap) {
        kernel::print("PMM: Failed to allocate memory for bitmap!\n");
        return;
    }

    // Mark usable areas in bitmap
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].type == 1) { // Usable
            uintptr_t start_page = entries[i].base / PAGE_SIZE;
            size_t page_count = entries[i].length / PAGE_SIZE;
            for (size_t j = 0; j < page_count; ++j) {
                size_t page_idx = start_page + j;
                g_bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
            }
        }
    }

    // Mark bitmap pages themselves as used
    uintptr_t bitmap_start = reinterpret_cast<uintptr_t>(g_bitmap) / PAGE_SIZE;
    size_t bitmap_pages = g_bitmap_size / PAGE_SIZE;
    if (g_bitmap_size % PAGE_SIZE != 0) bitmap_pages++;
    for (size_t i = 0; i < bitmap_pages; ++i) {
        size_t page_idx = bitmap_start + i;
        g_bitmap[page_idx / 8] |= (1 << (page_idx % 8));
    }

    g_used_pages = 0;
    for (size_t i = 0; i < g_total_pages; ++i) {
        if (g_bitmap[i / 8] & (1 << (i % 8))) {
            g_used_pages++;
        }
    }
}

void* pmm::alloc_page() noexcept {
    return alloc_pages(1);
}

void* pmm::alloc_pages(size_t count) noexcept {
    if (count == 0) return nullptr;

    for (size_t i = 0; i <= g_total_pages - count; ++i) {
        size_t start_idx = (g_last_search_idx + i) % (g_total_pages - count + 1);

        bool found = true;
        for (size_t j = 0; j < count; ++j) {
            size_t idx = start_idx + j;
            if (g_bitmap[idx / 8] & (1 << (idx % 8))) {
                found = false;
                break;
            }
        }

        if (found) {
            for (size_t j = 0; j < count; ++j) {
                size_t idx = start_idx + j;
                g_bitmap[idx / 8] |= (1 << (idx % 8));
            }
            g_used_pages += count;
            g_last_search_idx = start_idx + count;
            return reinterpret_cast<void*>(start_idx * PAGE_SIZE);
        }
    }
    kernel::print("PMM: Out of memory (requested {} pages)!\n", count);
    return nullptr;
}

void pmm::free_page(void* ptr) noexcept {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if (addr % PAGE_SIZE != 0) return;
    size_t idx = addr / PAGE_SIZE;
    if (idx >= g_total_pages) return;

    if (g_bitmap[idx / 8] & (1 << (idx % 8))) {
        g_bitmap[idx / 8] &= ~(1 << (idx % 8));
        g_used_pages--;
    }
}

size_t pmm::get_total_pages() noexcept {
    return g_total_pages;
}
size_t pmm::get_used_pages() noexcept {
    return g_used_pages;
}
size_t pmm::get_free_pages() noexcept {
    return g_total_pages - g_used_pages;
}

} // namespace kernel::memory
