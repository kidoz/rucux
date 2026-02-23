// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

class pmm {
public:
    static constexpr size_t PAGE_SIZE = 4096;

    struct memory_map_entry {
        uintptr_t base;
        size_t length;
        uint32_t type;
    };

    static void init(const memory_map_entry* entries, size_t entry_count) noexcept;
    static void* alloc_page() noexcept;
    static void* alloc_pages(size_t count) noexcept;
    static void free_page(void* ptr) noexcept;

    static size_t get_total_pages() noexcept;
    static size_t get_used_pages() noexcept;
    static size_t get_free_pages() noexcept;
};

} // namespace kernel::memory
