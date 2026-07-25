// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/cpu/percpu.hpp>
#include <kernel/ds/list.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

// Slab allocator: fixed-size object caches backed by buddy-allocated pages.
// Per-CPU magazines provide a lock-free fast path for alloc/free.

// A slab is a contiguous block of pages divided into equal-sized objects.
struct slab {
    list_node link;  // Link in partial/full/free slab list
    void* free_list; // Freelist of objects within this slab
    uint16_t in_use; // Number of allocated objects
    uint16_t total;  // Total objects in this slab
    void* base;      // Start of object array
};

// Per-CPU magazine: small stack of recently freed objects.
// Alloc/free from the magazine requires no locks (single-CPU access with IRQs off).
struct magazine {
    static constexpr size_t CAPACITY = 32;
    void* objects[CAPACITY];
    uint32_t count;
};

class slab_cache {
public:
    // Create a cache for objects of `obj_size` bytes, aligned to `align`.
    void init(const char* name, size_t obj_size, size_t align = 8) noexcept;

    // Allocate one object from this cache
    void* alloc() noexcept;

    // Free one object back to this cache
    void free(void* ptr) noexcept;

    const char* name() const noexcept { return name_; }
    size_t object_size() const noexcept { return obj_size_; }

private:
    // Slow path: allocate a new slab from the buddy allocator
    slab* grow() noexcept;

    // Return object to slab (used when magazine is full)
    void free_to_slab(void* ptr) noexcept;

    // Alloc from slab lists (used when magazine is empty)
    void* alloc_from_slab() noexcept;

    const char* name_;
    size_t obj_size_;      // Actual object size (after alignment)
    size_t slab_order_;    // Buddy order for slab pages
    size_t objs_per_slab_; // Objects per slab

    list_node partial_; // Slabs with some free objects
    list_node full_;    // Slabs with no free objects
    list_node free_;    // Slabs with all objects free (reclaimable)
    irq_spinlock lock_; // Protects slab lists

    magazine magazines_[cpu::MAX_CPUS]; // Per-CPU magazine
};

// ─── Global slab infrastructure ────────────────────────────────────────────

// Initialize the slab allocator subsystem
void slab_init() noexcept;

// Generic kmalloc backed by size-class slab caches (8, 16, 32, 64, 128, 256, 512, 1024, 2048)
void* slab_alloc(size_t size) noexcept;
void slab_free(void* ptr, size_t size) noexcept;

// Named caches for hot kernel objects
slab_cache& thread_cache() noexcept;
slab_cache& vfs_node_cache() noexcept;

} // namespace kernel::memory
