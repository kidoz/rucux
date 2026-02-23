// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

heap::block_header* heap::g_head = nullptr;

void heap::init() noexcept {
    // Initial heap: allocate 2 MB (512 pages)
    void* initial_pages = pmm::alloc_pages(512);
    if (!initial_pages) {
        kernel::print("Failed to allocate initial heap!\n");
        return;
    }

    g_head = static_cast<block_header*>(initial_pages);
    g_head->size = (512 * pmm::PAGE_SIZE) - sizeof(block_header);
    g_head->is_free = true;
    g_head->next = nullptr;
    g_head->magic = HEAP_MAGIC;

    kernel::print("Kernel Heap Initialized at {}, size {} KB\n", g_head, 2048);
}

void* heap::kmalloc(size_t size) noexcept {
    // Basic First-Fit Allocator
    // 8-byte alignment
    size = (size + 7) & ~7ULL;

    block_header* current = g_head;
    while (current) {
        if (current->is_free && current->size >= size) {
            // Split block if there's enough leftover for another header + some data
            if (current->size >= size + sizeof(block_header) + 8) {
                block_header* new_block =
                    reinterpret_cast<block_header*>(reinterpret_cast<uint8_t*>(current) + sizeof(block_header) + size);

                new_block->size = current->size - size - sizeof(block_header);
                new_block->is_free = true;
                new_block->next = current->next;
                new_block->magic = HEAP_MAGIC;

                current->size = size;
                current->next = new_block;
            }

            current->is_free = false;
            return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(current) + sizeof(block_header));
        }
        current = current->next;
    }

    kernel::print("kmalloc: Out of heap memory for size {}!\n", size);
    return nullptr;
}

void heap::kfree(void* ptr) noexcept {
    if (!ptr) return;

    block_header* header = reinterpret_cast<block_header*>(reinterpret_cast<uint8_t*>(ptr) - sizeof(block_header));

    if (header->magic != HEAP_MAGIC) {
        kernel::print("kfree: Invalid block magic at {}!\n", ptr);
        return;
    }

    header->is_free = true;

    // Coalesce with next block if free
    if (header->next && header->next->is_free) {
        header->size += sizeof(block_header) + header->next->size;
        header->next = header->next->next;
    }

    // Coalesce with previous block (requires a full scan or a doubly linked list)
    block_header* prev = g_head;
    while (prev && prev->next != header) {
        prev = prev->next;
    }
    if (prev && prev->is_free) {
        prev->size += sizeof(block_header) + header->size;
        prev->next = header->next;
    }
}

} // namespace kernel::memory

extern "C" {
void* kmalloc(size_t size) noexcept {
    return kernel::memory::heap::kmalloc(size);
}
void kfree(void* ptr) noexcept {
    kernel::memory::heap::kfree(ptr);
}
}
