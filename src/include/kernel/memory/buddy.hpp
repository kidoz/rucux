// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

// Buddy allocator for physical page frames.
// Manages free pages in power-of-2 blocks (orders 0..MAX_ORDER).
// Order 0 = 1 page (4KB), Order 1 = 2 pages (8KB), ..., Order 10 = 1024 pages (4MB).
class buddy_allocator {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr uint32_t MAX_ORDER = 10; // Up to 4MB blocks

    struct free_block {
        free_block* next;
        uint32_t order;
    };

    void init(uintptr_t base, size_t total_pages) noexcept;

    // Allocate 2^order contiguous pages. Returns physical address or 0 on failure.
    uintptr_t alloc(uint32_t order) noexcept;

    // Free 2^order contiguous pages starting at addr.
    void free(uintptr_t addr, uint32_t order) noexcept;

    // Convenience: allocate exactly 1 page
    uintptr_t alloc_page() noexcept { return alloc(0); }
    void free_page(uintptr_t addr) noexcept { free(addr, 0); }

    // Allocate N contiguous pages (finds smallest order that fits)
    uintptr_t alloc_pages(size_t count) noexcept;

    size_t get_free_pages() const noexcept { return free_pages_; }
    size_t get_total_pages() const noexcept { return total_pages_; }

    // Order required to hold at least `count` pages
    static uint32_t pages_to_order(size_t count) noexcept;

private:
    // Get buddy address for a block at addr of given order
    uintptr_t buddy_of(uintptr_t addr, uint32_t order) const noexcept;

    // Check if page at addr is marked as free in bitmap
    bool is_free(uintptr_t addr, uint32_t order) const noexcept;

    // Toggle the buddy-pair bit (used to detect if buddy is free for coalescing)
    void toggle_bit(uintptr_t addr, uint32_t order) noexcept;

    free_block* free_lists_[MAX_ORDER + 1]{};
    uint8_t* buddy_bitmap_;    // Per-order bitmap (non-overlapping regions)
    size_t bitmap_offsets_[MAX_ORDER + 1]{}; // Bit offset for each order
    uintptr_t base_;           // Start of managed physical memory
    size_t total_pages_;
    size_t free_pages_;
    kernel::irq_spinlock lock_;
};

} // namespace kernel::memory
