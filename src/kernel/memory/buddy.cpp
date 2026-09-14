// SPDX-License-Identifier: MIT
#include <kernel/memory/buddy.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

uint32_t buddy_allocator::pages_to_order(size_t count) noexcept {
    if (count <= 1) return 0;
    if (count > (1UL << MAX_ORDER)) return MAX_ORDER + 1;
    uint32_t order = 0;
    size_t size = 1;
    while (size < count) {
        size <<= 1;
        order++;
    }
    return order;
}

uintptr_t buddy_allocator::buddy_of(uintptr_t addr, uint32_t order) const noexcept {
    // Free blocks are aligned to physical addresses, including arenas whose
    // base is not aligned to their largest order.
    return addr ^ (PAGE_SIZE << order);
}

void buddy_allocator::init(uintptr_t base, size_t total_pages) noexcept {
    base_ = base;
    total_pages_ = total_pages;
    free_pages_ = 0;

    for (uint32_t i = 0; i <= MAX_ORDER; ++i)
        free_lists_[i] = nullptr;

    // Add all managed pages as free blocks, starting from the highest order
    uintptr_t addr = base;
    size_t remaining = total_pages;

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

    kernel::print("Buddy: {} pages managed ({} MB)\n", free_pages_, (free_pages_ * PAGE_SIZE) / (1024 * 1024));
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
    }

    free_pages_ -= (1UL << order);
    return addr;
}

void buddy_allocator::free(uintptr_t addr, uint32_t order) noexcept {
    if (addr == 0 || order > MAX_ORDER || addr < base_ || addr % (PAGE_SIZE << order) != 0 ||
        (addr - base_) / PAGE_SIZE >= total_pages_ || (1UL << order) > total_pages_ - (addr - base_) / PAGE_SIZE)
        return;

    kernel::irq_lock_guard guard(lock_);

    free_pages_ += (1UL << order);

    // Membership in the same-order free list proves the buddy is free.
    // The former parity bitmap could claim a buddy was free even when it was
    // absent from this list, merging live pages into an allocatable block.
    while (order < MAX_ORDER) {
        uintptr_t buddy_addr = buddy_of(addr, order);
        free_block** link = &free_lists_[order];
        while (*link && reinterpret_cast<uintptr_t>(*link) != buddy_addr)
            link = &(*link)->next;
        if (!*link) break;
        *link = (*link)->next;
        if (buddy_addr < addr) addr = buddy_addr;
        ++order;
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
