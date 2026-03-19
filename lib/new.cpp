// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/memory/slab.hpp>
#include <knew.hpp>

// Threshold: objects <= 2048 bytes use slab, larger use heap
static constexpr size_t SLAB_MAX = 2048;

void* operator new(size_t size) {
    void* p = kernel::memory::slab_alloc(size);
    if (p) return p;
    return kmalloc(size); // Fallback to heap for large or pre-init allocs
}

void* operator new[](size_t size) {
    void* p = kernel::memory::slab_alloc(size);
    if (p) return p;
    return kmalloc(size);
}

void operator delete(void* p) noexcept {
    kfree(p); // Can't determine size → heap free (safe: heap checks magic)
}

void operator delete[](void* p) noexcept {
    kfree(p);
}

void operator delete(void* p, size_t size) noexcept {
    if (size <= SLAB_MAX) {
        kernel::memory::slab_free(p, size);
        return;
    }
    kfree(p);
}

void operator delete[](void* p, size_t size) noexcept {
    if (size <= SLAB_MAX) {
        kernel::memory::slab_free(p, size);
        return;
    }
    kfree(p);
}
