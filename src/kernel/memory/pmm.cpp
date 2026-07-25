// SPDX-License-Identifier: MIT
#include <kernel/cpu/percpu.hpp>
#include <kernel/memory/buddy.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/print.hpp>

namespace kernel::memory {

static buddy_allocator g_buddy;
static size_t g_total_pages;

// ─── Per-CPU page cache ────────────────────────────────────────────────────
// A lock-free fast path for single-page alloc/free on the local CPU.
// Avoids contending on the global buddy lock for the most common case.

static constexpr size_t PCPU_CACHE_SIZE = 64;

struct page_cache {
    uintptr_t pages[PCPU_CACHE_SIZE];
    uint32_t count;
};

// One cache per CPU. Indexed by cpu_id.
static page_cache g_pcpu_caches[cpu::MAX_CPUS] = {};

// Refill the local cache from the buddy allocator (batch alloc)
static void cache_refill(page_cache& cache) noexcept {
    constexpr uint32_t BATCH = PCPU_CACHE_SIZE / 2;
    for (uint32_t i = 0; i < BATCH; ++i) {
        uintptr_t p = g_buddy.alloc_page();
        if (!p) break;
        cache.pages[cache.count++] = p;
    }
}

// Drain half the local cache back to the buddy allocator
static void cache_drain(page_cache& cache) noexcept {
    uint32_t drain = cache.count / 2;
    for (uint32_t i = 0; i < drain; ++i) {
        cache.count--;
        g_buddy.free_page(cache.pages[cache.count]);
    }
}

// ─── PMM interface ─────────────────────────────────────────────────────────

void pmm::init(const memory_map_entry* entries, size_t entry_count) noexcept {
    uintptr_t best_base = 0;
    size_t best_length = 0;

    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].type == 1 && entries[i].length > best_length) {
            best_base = entries[i].base;
            best_length = entries[i].length;
        }
    }

    if (best_length == 0) {
        kernel::print("PMM: No usable memory found!\n");
        return;
    }

    if (best_base % PAGE_SIZE) {
        size_t waste = PAGE_SIZE - (best_base % PAGE_SIZE);
        best_base += waste;
        best_length -= waste;
    }

    size_t pages = best_length / PAGE_SIZE;
    g_total_pages = pages;

    g_buddy.init(best_base, pages);

    kernel::print("PMM: Buddy allocator initialized, {} MB free\n",
                  (g_buddy.get_free_pages() * PAGE_SIZE) / (1024 * 1024));
}

void* pmm::alloc_page() noexcept {
    // Fast path: try per-CPU cache (interrupts must be disabled to prevent preemption)
    uintptr_t flags = kernel::irq_save();
    auto* pcpu = cpu::this_cpu();
    auto& cache = g_pcpu_caches[pcpu->cpu_id];

    if (cache.count == 0) cache_refill(cache);

    uintptr_t addr = 0;
    if (cache.count > 0) {
        cache.count--;
        addr = cache.pages[cache.count];
    }
    kernel::irq_restore(flags);

    return addr ? reinterpret_cast<void*>(addr) : nullptr;
}

void* pmm::alloc_pages(size_t count) noexcept {
    if (count == 0) return nullptr;
    if (count == 1) return alloc_page(); // Use cache fast path
    // Multi-page: go straight to buddy (can't cache arbitrary orders)
    uintptr_t addr = g_buddy.alloc_pages(count);
    return addr ? reinterpret_cast<void*>(addr) : nullptr;
}

void pmm::free_page(void* ptr) noexcept {
    if (!ptr) return;

    uintptr_t flags = kernel::irq_save();
    auto* pcpu = cpu::this_cpu();
    auto& cache = g_pcpu_caches[pcpu->cpu_id];

    if (cache.count >= PCPU_CACHE_SIZE) cache_drain(cache);

    cache.pages[cache.count++] = reinterpret_cast<uintptr_t>(ptr);
    kernel::irq_restore(flags);
}

size_t pmm::get_total_pages() noexcept {
    return g_total_pages;
}

size_t pmm::get_used_pages() noexcept {
    return g_total_pages - g_buddy.get_free_pages();
}

size_t pmm::get_free_pages() noexcept {
    return g_buddy.get_free_pages();
}

} // namespace kernel::memory
