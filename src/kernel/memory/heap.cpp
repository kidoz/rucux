// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

heap::block_header* heap::g_free_list = nullptr;

void heap::add_to_free_list(heap::block_header* block) noexcept {
    block->prev_free = nullptr;
    block->next_free = heap::g_free_list;
    if (heap::g_free_list) {
        heap::g_free_list->prev_free = block;
    }
    heap::g_free_list = block;
}

void heap::remove_from_free_list(heap::block_header* block) noexcept {
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        heap::g_free_list = block->next_free;
    }
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
}

void heap::init() noexcept {
    void* initial_pages = pmm::alloc_pages(512);
    if (!initial_pages) {
        kernel::print("Failed to allocate initial heap!\n");
        return;
    }

    block_header* head = static_cast<block_header*>(initial_pages);
    head->size = (512 * pmm::PAGE_SIZE) - sizeof(block_header);
    head->is_free = true;
    head->magic = HEAP_MAGIC;
    head->prev_phys = nullptr;
    head->next_phys = nullptr;

    g_free_list = nullptr;
    add_to_free_list(head);

    kernel::print("Kernel Heap Initialized at {}, size {} KB\n", head, 2048);
}

void* heap::kmalloc(size_t size) noexcept {
    size = (size + 7) & ~7ULL;

    block_header* current = g_free_list;
    while (current) {
        if (current->size >= size) {
            // Split if enough space
            if (current->size >= size + sizeof(block_header) + 8) {
                block_header* new_block =
                    reinterpret_cast<block_header*>(reinterpret_cast<uint8_t*>(current) + sizeof(block_header) + size);

                new_block->size = current->size - size - sizeof(block_header);
                new_block->is_free = true;
                new_block->magic = HEAP_MAGIC;
                
                new_block->prev_phys = current;
                new_block->next_phys = current->next_phys;
                if (new_block->next_phys) {
                    new_block->next_phys->prev_phys = new_block;
                }
                current->next_phys = new_block;
                current->size = size;

                add_to_free_list(new_block);
            }

            current->is_free = false;
            remove_from_free_list(current);
            return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(current) + sizeof(block_header));
        }
        current = current->next_free;
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

    if (header->is_free) {
        kernel::print("kfree: Double free detected at {}!\n", ptr);
        return;
    }

    header->is_free = true;
    add_to_free_list(header);

    // Coalesce with next
    if (header->next_phys && header->next_phys->is_free) {
        block_header* next = header->next_phys;
        remove_from_free_list(next);
        header->size += sizeof(block_header) + next->size;
        header->next_phys = next->next_phys;
        if (header->next_phys) {
            header->next_phys->prev_phys = header;
        }
    }

    // Coalesce with prev
    if (header->prev_phys && header->prev_phys->is_free) {
        block_header* prev = header->prev_phys;
        remove_from_free_list(header);
        prev->size += sizeof(block_header) + header->size;
        prev->next_phys = header->next_phys;
        if (prev->next_phys) {
            prev->next_phys->prev_phys = prev;
        }
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
