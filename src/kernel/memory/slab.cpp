// SPDX-License-Identifier: MIT
#include <kernel/memory/buddy.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/slab.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

// ─── slab_cache ────────────────────────────────────────────────────────────

void slab_cache::init(const char* name, size_t obj_size, size_t align) noexcept {
    name_ = name;

    // Round up object size to alignment
    obj_size_ = (obj_size + align - 1) & ~(align - 1);
    // Minimum object size must hold a freelist pointer
    if (obj_size_ < sizeof(void*)) obj_size_ = sizeof(void*);

    // Determine slab order: fit at least 8 objects per slab, minimum 1 page
    slab_order_ = 0;
    while (true) {
        size_t slab_bytes = pmm::PAGE_SIZE << slab_order_;
        // Reserve space for the slab header at the end of the slab
        size_t usable = slab_bytes - sizeof(slab);
        objs_per_slab_ = usable / obj_size_;
        if (objs_per_slab_ >= 8 || slab_order_ >= 4) break;
        slab_order_++;
    }

    partial_.init();
    full_.init();
    free_.init();

    for (uint32_t i = 0; i < cpu::MAX_CPUS; ++i)
        magazines_[i].count = 0;
}

slab* slab_cache::grow() noexcept {
    // Allocate pages from PMM
    void* pages = pmm::alloc_pages(1UL << slab_order_);
    if (!pages) return nullptr;

    size_t slab_bytes = pmm::PAGE_SIZE << slab_order_;

    // Place slab metadata at the end of the allocated pages
    auto* s = reinterpret_cast<slab*>(
        reinterpret_cast<uintptr_t>(pages) + slab_bytes - sizeof(slab));
    s->base = pages;
    s->in_use = 0;
    s->total = static_cast<uint16_t>(objs_per_slab_);
    s->link.init();

    // Build per-slab freelist
    s->free_list = nullptr;
    auto* obj = reinterpret_cast<uint8_t*>(pages);
    for (size_t i = 0; i < objs_per_slab_; ++i) {
        *reinterpret_cast<void**>(obj) = s->free_list;
        s->free_list = obj;
        obj += obj_size_;
    }

    return s;
}

void* slab_cache::alloc_from_slab() noexcept {
    irq_lock_guard guard(lock_);

    // Try partial slabs first
    if (!partial_.empty()) {
        auto* s = container_of(partial_.next, &slab::link);
        void* obj = s->free_list;
        s->free_list = *reinterpret_cast<void**>(obj);
        s->in_use++;
        if (!s->free_list) {
            s->link.remove();
            full_.push_back(&s->link);
        }
        return obj;
    }

    // Try free slabs
    if (!free_.empty()) {
        auto* s = container_of(free_.next, &slab::link);
        s->link.remove();
        void* obj = s->free_list;
        s->free_list = *reinterpret_cast<void**>(obj);
        s->in_use++;
        partial_.push_back(&s->link);
        return obj;
    }

    // Need to grow — allocate outside the lock to avoid holding it too long
    // But we're already holding it... keep it simple for now.
    auto* s = grow();
    if (!s) return nullptr;

    void* obj = s->free_list;
    s->free_list = *reinterpret_cast<void**>(obj);
    s->in_use++;
    partial_.push_back(&s->link);
    return obj;
}

void slab_cache::free_to_slab(void* ptr) noexcept {
    // Find which slab this object belongs to by rounding down to slab boundary
    size_t slab_bytes = pmm::PAGE_SIZE << slab_order_;
    uintptr_t slab_base = reinterpret_cast<uintptr_t>(ptr) & ~(slab_bytes - 1);
    auto* s = reinterpret_cast<slab*>(slab_base + slab_bytes - sizeof(slab));

    irq_lock_guard guard(lock_);

    // Add object back to slab freelist
    *reinterpret_cast<void**>(ptr) = s->free_list;
    s->free_list = ptr;
    s->in_use--;

    if (s->in_use == 0) {
        // Move to free list (could be reclaimed later)
        s->link.remove();
        free_.push_back(&s->link);
    } else if (s->in_use == s->total - 1) {
        // Was full, now partial
        s->link.remove();
        partial_.push_back(&s->link);
    }
}

void* slab_cache::alloc() noexcept {
    // Fast path: per-CPU magazine
    uintptr_t flags = irq_save();
    auto* pcpu = cpu::this_cpu();
    auto& mag = magazines_[pcpu->cpu_id];
    if (mag.count > 0) {
        void* obj = mag.objects[--mag.count];
        irq_restore(flags);
        return obj;
    }
    irq_restore(flags);

    // Slow path: allocate from slab
    return alloc_from_slab();
}

void slab_cache::free(void* ptr) noexcept {
    if (!ptr) return;

    // Fast path: per-CPU magazine
    uintptr_t flags = irq_save();
    auto* pcpu = cpu::this_cpu();
    auto& mag = magazines_[pcpu->cpu_id];
    if (mag.count < magazine::CAPACITY) {
        mag.objects[mag.count++] = ptr;
        irq_restore(flags);
        return;
    }
    irq_restore(flags);

    // Slow path: return to slab
    free_to_slab(ptr);
}

// ─── Generic size-class caches ─────────────────────────────────────────────

static constexpr size_t NUM_SIZE_CLASSES = 9;
static constexpr size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048
};

static slab_cache g_size_caches[NUM_SIZE_CLASSES];
static slab_cache g_thread_cache;
static slab_cache g_vfs_node_cache;
static bool g_slab_initialized = false;

void slab_init() noexcept {
    const char* names[] = {"slab-8", "slab-16", "slab-32", "slab-64",
                           "slab-128", "slab-256", "slab-512", "slab-1024", "slab-2048"};
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i)
        g_size_caches[i].init(names[i], SIZE_CLASSES[i]);

    // Named caches for hot objects
    g_thread_cache.init("thread", 512); // thread struct is ~400 bytes
    g_vfs_node_cache.init("vfs_node", 64); // vfs_node is now ~48 bytes

    g_slab_initialized = true;
    kernel::print("Slab: {} size-class caches initialized\n", NUM_SIZE_CLASSES);
}

static size_t size_to_class(size_t size) noexcept {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i)
        if (size <= SIZE_CLASSES[i]) return i;
    return NUM_SIZE_CLASSES; // Too large for slab
}

void* slab_alloc(size_t size) noexcept {
    if (!g_slab_initialized) return nullptr;
    size_t cls = size_to_class(size);
    if (cls >= NUM_SIZE_CLASSES) return nullptr; // Use heap for large allocs
    return g_size_caches[cls].alloc();
}

void slab_free(void* ptr, size_t size) noexcept {
    if (!ptr || !g_slab_initialized) return;
    size_t cls = size_to_class(size);
    if (cls >= NUM_SIZE_CLASSES) return;
    g_size_caches[cls].free(ptr);
}

slab_cache& thread_cache() noexcept { return g_thread_cache; }
slab_cache& vfs_node_cache() noexcept { return g_vfs_node_cache; }

} // namespace kernel::memory
