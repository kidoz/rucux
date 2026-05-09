// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>
#include <kernel/sync/spinlock.hpp>

namespace kernel::memory {

class heap {
public:
    static void init() noexcept;

    static void* kmalloc(size_t size) noexcept;
    static void kfree(void* ptr) noexcept;

private:
    struct alignas(16) block_header {
        size_t size;
        bool is_free;
        uint32_t magic;
        block_header* prev_phys;
        block_header* next_phys;
        block_header* prev_free;
        block_header* next_free;
    };

    static constexpr uint32_t HEAP_MAGIC = 0xDEADC0DE;
    static block_header* g_free_list;
    static kernel::irq_spinlock g_lock;

    static void add_to_free_list(block_header* block) noexcept;
    static void remove_from_free_list(block_header* block) noexcept;
};

} // namespace kernel::memory

extern "C" {
void* kmalloc(size_t size) noexcept;
void kfree(void* ptr) noexcept;
}
