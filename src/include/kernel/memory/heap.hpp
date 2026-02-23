// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

class heap {
public:
    static void init() noexcept;

    static void* kmalloc(size_t size) noexcept;
    static void kfree(void* ptr) noexcept;

private:
    struct block_header {
        size_t size;
        bool is_free;
        block_header* next;
        uint32_t magic;
    };

    static constexpr uint32_t HEAP_MAGIC = 0xDEADC0DE;
    static block_header* g_head;
};

} // namespace kernel::memory

extern "C" {
void* kmalloc(size_t size) noexcept;
void kfree(void* ptr) noexcept;
}
