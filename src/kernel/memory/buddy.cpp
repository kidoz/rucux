// SPDX-License-Identifier: MIT
#include <kernel/memory/buddy.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

uint32_t buddy_allocator::pages_to_order(size_t count) noexcept {
    if (count <= 1) return 0;
    uint32_t order = 0;
    size_t size = 1;
    while (size < count) {
        size <<= 1;
        order++;
    }
    return order;
}

uintptr_t buddy_allocator::buddy_of(uintptr_t addr, uint32_t order) const noexcept {
    uintptr_t offset = addr - base_;
    uintptr_t buddy_offset = offset ^ (PAGE_SIZE << order);
    return base_ + buddy_offset;
}

// Each order has its own region in the bitmap to avoid aliasing.
// bitmap_offsets_[order] gives the starting BIT offset for that order.
// Within an order, pair_index = page_index / (2^(order+1)).
void buddy_allocator::toggle_bit(uintptr_t addr, uint32_t order) noexcept {
    size_t page_idx = (addr - base_) / PAGE_SIZE;
    size_t pair_idx = page_idx >> (order + 1);
    size_t bit = bitmap_offsets_[order] + pair_idx;
    buddy_bitmap_[bit / 8] ^= (1 << (bit % 8));
}

bool buddy_allocator::is_free(uintptr_t addr, uint32_t order) const noexcept {
    size_t page_idx = (addr - base_) / PAGE_SIZE;
    size_t pair_idx = page_idx >> (order + 1);
    size_t bit = bitmap_offsets_[order] + pair_idx;
    return (buddy_bitmap_[bit / 8] >> (bit % 8)) & 1;
}

void buddy_allocator::init(uintptr_t base, size_t total_pages) noexcept {
    base_ = base;
    total_pages_ = total_pages;
    free_pages_ = 0;

    for (uint32_t i = 0; i <= MAX_ORDER; ++i)
        free_lists_[i] = nullptr;

    // Calculate per-order bitmap offsets (in bits).
    // Order k needs total_pages / 2^(k+1) bits.
    size_t total_bits = 0;
    for (uint32_t k = 0; k <= MAX_ORDER; ++k) {
        bitmap_offsets_[k] = total_bits;
        size_t bits_for_order = total_pages >> (k + 1);
        if (bits_for_order == 0) bits_for_order = 1;
        total_bits += bits_for_order;
    }
    size_t bitmap_bytes = total_bits / 8 + 1;
    size_t bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    buddy_bitmap_ = reinterpret_cast<uint8_t*>(base);
    lib::memset(buddy_bitmap_, 0, bitmap_bytes);

    // The managed area starts after the bitmap
    uintptr_t managed_start = base + bitmap_pages * PAGE_SIZE;
    size_t managed_pages = total_pages > bitmap_pages ? total_pages - bitmap_pages : 0;

    // Add all managed pages as free blocks, starting from the highest order
    uintptr_t addr = managed_start;
    size_t remaining = managed_pages;

    while (remaining > 0) {
        // Find the largest order block that:
        // 1) fits in remaining pages
        // 2) is naturally aligned to that order
        uint32_t order = MAX_ORDER;
        while (order > 0) {
            size_t block_pages = 1UL << order;
            uintptr_t alignment = block_pages * PAGE_SIZE;
            if (block_pages <= remaining && (addr % alignment) == 0) break;
            order--;
        }

        size_t block_pages = 1UL << order;

        // Add to free list
        auto* block = reinterpret_cast<free_block*>(addr);
        block->order = order;
        block->next = free_lists_[order];
        free_lists_[order] = block;

        free_pages_ += block_pages;
        addr += block_pages * PAGE_SIZE;
        remaining -= block_pages;
    }

    kernel::print("Buddy: {} pages managed ({} MB), bitmap {} pages\n", free_pages_,
                  (free_pages_ * PAGE_SIZE) / (1024 * 1024), bitmap_pages);
}

uintptr_t buddy_allocator::alloc(uint32_t order) noexcept {
    if (order > MAX_ORDER) return 0;

    kernel::irq_lock_guard guard(lock_);

    // Find the smallest order with a free block
    uint32_t current = order;
    while (current <= MAX_ORDER && !free_lists_[current])
        current++;

    if (current > MAX_ORDER) return 0; // Out of memory

    // Remove block from free list
    free_block* block = free_lists_[current];
    free_lists_[current] = block->next;

    uintptr_t addr = reinterpret_cast<uintptr_t>(block);

    // Split down to requested order
    while (current > order) {
        current--;
        uintptr_t buddy_addr = addr + (PAGE_SIZE << current);
        auto* buddy = reinterpret_cast<free_block*>(buddy_addr);
        buddy->order = current;
        buddy->next = free_lists_[current];
        free_lists_[current] = buddy;
        toggle_bit(addr, current);
    }

    toggle_bit(addr, order);
    free_pages_ -= (1UL << order);
    return addr;
}

void buddy_allocator::free(uintptr_t addr, uint32_t order) noexcept {
    if (addr == 0 || order > MAX_ORDER) return;

    kernel::irq_lock_guard guard(lock_);

    free_pages_ += (1UL << order);

    // Try to coalesce with buddy
    while (order < MAX_ORDER) {
        toggle_bit(addr, order);

        // If the bit is now 1, the buddy is still allocated → can't coalesce
        if (is_free(addr, order)) break;

        // Buddy is free → remove it from its free list and coalesce
        uintptr_t buddy_addr = buddy_of(addr, order);

        // Remove buddy from free list
        free_block** pp = &free_lists_[order];
        while (*pp) {
            if (reinterpret_cast<uintptr_t>(*pp) == buddy_addr) {
                *pp = (*pp)->next;
                break;
            }
            pp = &(*pp)->next;
        }

        // Merge: use the lower address as the new block
        if (buddy_addr < addr) addr = buddy_addr;
        order++;
    }

    // Insert into free list
    auto* block = reinterpret_cast<free_block*>(addr);
    block->order = order;
    block->next = free_lists_[order];
    free_lists_[order] = block;
}

uintptr_t buddy_allocator::alloc_pages(size_t count) noexcept {
    if (count == 0) return 0;
    return alloc(pages_to_order(count));
}

} // namespace kernel::memory
