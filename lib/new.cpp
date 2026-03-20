// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/memory/slab.hpp>
#include <knew.hpp>

// The unsized new/delete path ALWAYS uses the heap (kmalloc/kfree).
// This ensures any pointer from `new` can be freed by `delete` without
// needing to know the allocation source.
//
// The sized-delete path (C++14) can route small objects to slab caches,
// but only if slab_alloc was explicitly used (e.g., via named caches).
// For general `new`, we stick to heap for safety.

void* operator new(size_t size) {
    return kmalloc(size);
}

void* operator new[](size_t size) {
    return kmalloc(size);
}

void operator delete(void* p) noexcept {
    kfree(p);
}

void operator delete[](void* p) noexcept {
    kfree(p);
}

void operator delete(void* p, size_t) noexcept {
    kfree(p);
}

void operator delete[](void* p, size_t) noexcept {
    kfree(p);
}
