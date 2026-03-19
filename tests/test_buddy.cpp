// SPDX-License-Identifier: MIT
// Host-compiled test for buddy allocator.
// We provide minimal stubs to avoid pulling in conflicting kernel headers.

#include "test_harness.hpp"
#include <cstdint>
#include <cstring>
#include <new>

// ── Stubs for kernel dependencies ──────────────────────────────────────────
// Provide the minimal surface that buddy.hpp / buddy.cpp need.

// Stub kernel::atomic (use std::atomic semantics via __atomic builtins)
namespace kernel {
enum memory_order {
    relaxed = __ATOMIC_RELAXED, acquire = __ATOMIC_ACQUIRE,
    release = __ATOMIC_RELEASE, seq_cst = __ATOMIC_SEQ_CST,
    consume = __ATOMIC_CONSUME, acq_rel = __ATOMIC_ACQ_REL,
};
template<typename T> class atomic {
    T v_{};
public:
    constexpr atomic() noexcept = default;
    constexpr explicit atomic(T val) noexcept : v_{val} {}
    atomic(const atomic&) = delete;
    T load(memory_order o = seq_cst) const noexcept { return __atomic_load_n(&v_, o); }
    void store(T val, memory_order o = seq_cst) noexcept { __atomic_store_n(&v_, val, o); }
    T fetch_add(T val, memory_order o = seq_cst) noexcept { return __atomic_fetch_add(&v_, val, o); }
    bool compare_exchange_strong(T& exp, T des, memory_order s = seq_cst, memory_order f = seq_cst) noexcept {
        return __atomic_compare_exchange_n(&v_, &exp, des, false, s, f);
    }
};
inline void cpu_relax() noexcept { asm volatile("" ::: "memory"); }
inline uintptr_t irq_save() noexcept { return 0; }
inline void irq_restore(uintptr_t) noexcept {}
class spinlock {
    atomic<uint16_t> next_{0}, now_{0};
public:
    void lock() noexcept { uint16_t t = next_.fetch_add(1, relaxed); while(now_.load(acquire) != t) cpu_relax(); }
    void unlock() noexcept { now_.fetch_add(1, release); }
};
class irq_spinlock {
    spinlock s_;
public:
    [[nodiscard]] uintptr_t lock() noexcept { s_.lock(); return 0; }
    void unlock(uintptr_t) noexcept { s_.unlock(); }
};
class irq_lock_guard {
    irq_spinlock& l_; uintptr_t f_;
public:
    explicit irq_lock_guard(irq_spinlock& l) noexcept : l_(l), f_(l.lock()) {}
    ~irq_lock_guard() noexcept { l_.unlock(f_); }
};
template<typename... Args> void print(const char*, Args...) noexcept {}
} // namespace kernel

namespace lib {
inline void* memset(void* s, int c, size_t n) { return ::memset(s, c, n); }
} // namespace lib

// Now include buddy header and implementation directly.
// buddy.hpp uses <kernel/sync/spinlock.hpp> and <lib/stddef.hpp> — we provide
// everything they define above, so we skip those includes.
#define KERNEL_SYNC_SPINLOCK_HPP  // skip spinlock.hpp include guard
#define KERNEL_SYNC_ATOMIC_HPP    // skip atomic.hpp include guard

// Manually define what buddy.hpp needs that's normally from lib/stddef.hpp
// (size_t is already available from <cstdint>/<cstdlib>)

// Include buddy.hpp — it will try to include spinlock.hpp but that's guarded
// by #pragma once which already matched our stubs above? No — pragma once is
// per-file. We need to include the actual buddy header.
// Actually — let's just inline the buddy class definition and implementation.

// ── Buddy allocator (from buddy.hpp) ──────────────────────────────────────
namespace kernel::memory {

class buddy_allocator {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr uint32_t MAX_ORDER = 10;
    struct free_block { free_block* next; uint32_t order; };
    void init(uintptr_t base, size_t total_pages) noexcept;
    uintptr_t alloc(uint32_t order) noexcept;
    void free(uintptr_t addr, uint32_t order) noexcept;
    uintptr_t alloc_page() noexcept { return alloc(0); }
    void free_page(uintptr_t addr) noexcept { free(addr, 0); }
    uintptr_t alloc_pages(size_t count) noexcept;
    size_t get_free_pages() const noexcept { return free_pages_; }
    size_t get_total_pages() const noexcept { return total_pages_; }
    static uint32_t pages_to_order(size_t count) noexcept;
private:
    uintptr_t buddy_of(uintptr_t addr, uint32_t order) const noexcept;
    bool is_free(uintptr_t addr, uint32_t order) const noexcept;
    void toggle_bit(uintptr_t addr, uint32_t order) noexcept;
    free_block* free_lists_[MAX_ORDER + 1]{};
    uint8_t* buddy_bitmap_;
    size_t bitmap_offsets_[MAX_ORDER + 1]{};
    uintptr_t base_;
    size_t total_pages_;
    size_t free_pages_;
    kernel::irq_spinlock lock_;
};

// ── Buddy implementation (from buddy.cpp) ─────────────────────────────────

uint32_t buddy_allocator::pages_to_order(size_t count) noexcept {
    if (count <= 1) return 0;
    uint32_t order = 0; size_t size = 1;
    while (size < count) { size <<= 1; order++; }
    return order;
}
uintptr_t buddy_allocator::buddy_of(uintptr_t addr, uint32_t order) const noexcept {
    return base_ + ((addr - base_) ^ (PAGE_SIZE << order));
}
void buddy_allocator::toggle_bit(uintptr_t addr, uint32_t order) noexcept {
    size_t pair_idx = ((addr - base_) / PAGE_SIZE) >> (order + 1);
    size_t bit = bitmap_offsets_[order] + pair_idx;
    buddy_bitmap_[bit / 8] ^= (1 << (bit % 8));
}
bool buddy_allocator::is_free(uintptr_t addr, uint32_t order) const noexcept {
    size_t pair_idx = ((addr - base_) / PAGE_SIZE) >> (order + 1);
    size_t bit = bitmap_offsets_[order] + pair_idx;
    return (buddy_bitmap_[bit / 8] >> (bit % 8)) & 1;
}
void buddy_allocator::init(uintptr_t base, size_t total_pages) noexcept {
    base_ = base; total_pages_ = total_pages; free_pages_ = 0;
    for (uint32_t i = 0; i <= MAX_ORDER; ++i) free_lists_[i] = nullptr;
    size_t total_bits = 0;
    for (uint32_t k = 0; k <= MAX_ORDER; ++k) {
        bitmap_offsets_[k] = total_bits;
        size_t bits = total_pages >> (k + 1);
        if (bits == 0) bits = 1;
        total_bits += bits;
    }
    size_t bitmap_bytes = total_bits / 8 + 1;
    size_t bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    buddy_bitmap_ = reinterpret_cast<uint8_t*>(base);
    lib::memset(buddy_bitmap_, 0, bitmap_bytes);
    uintptr_t managed_start = base + bitmap_pages * PAGE_SIZE;
    size_t managed_pages = total_pages > bitmap_pages ? total_pages - bitmap_pages : 0;
    uintptr_t addr = managed_start; size_t remaining = managed_pages;
    while (remaining > 0) {
        uint32_t order = MAX_ORDER;
        while (order > 0) {
            size_t bp = 1UL << order;
            if (bp <= remaining && (addr % (bp * PAGE_SIZE)) == 0) break;
            order--;
        }
        size_t bp = 1UL << order;
        auto* block = reinterpret_cast<free_block*>(addr);
        block->order = order; block->next = free_lists_[order]; free_lists_[order] = block;
        free_pages_ += bp; addr += bp * PAGE_SIZE; remaining -= bp;
    }
}
uintptr_t buddy_allocator::alloc(uint32_t order) noexcept {
    if (order > MAX_ORDER) return 0;
    kernel::irq_lock_guard guard(lock_);
    uint32_t cur = order;
    while (cur <= MAX_ORDER && !free_lists_[cur]) cur++;
    if (cur > MAX_ORDER) return 0;
    free_block* block = free_lists_[cur]; free_lists_[cur] = block->next;
    uintptr_t addr = reinterpret_cast<uintptr_t>(block);
    while (cur > order) { cur--;
        uintptr_t ba = addr + (PAGE_SIZE << cur);
        auto* b = reinterpret_cast<free_block*>(ba);
        b->order = cur; b->next = free_lists_[cur]; free_lists_[cur] = b;
        toggle_bit(addr, cur);
    }
    toggle_bit(addr, order); free_pages_ -= (1UL << order); return addr;
}
void buddy_allocator::free(uintptr_t addr, uint32_t order) noexcept {
    if (addr == 0 || order > MAX_ORDER) return;
    kernel::irq_lock_guard guard(lock_);
    free_pages_ += (1UL << order);
    while (order < MAX_ORDER) {
        toggle_bit(addr, order);
        if (is_free(addr, order)) break;
        uintptr_t ba = buddy_of(addr, order);
        free_block** pp = &free_lists_[order];
        while (*pp) { if (reinterpret_cast<uintptr_t>(*pp) == ba) { *pp = (*pp)->next; break; } pp = &(*pp)->next; }
        if (ba < addr) addr = ba;
        order++;
    }
    auto* block = reinterpret_cast<free_block*>(addr);
    block->order = order; block->next = free_lists_[order]; free_lists_[order] = block;
}
uintptr_t buddy_allocator::alloc_pages(size_t count) noexcept {
    if (count == 0) return 0; return alloc(pages_to_order(count));
}

} // namespace kernel::memory

// ── Tests ──────────────────────────────────────────────────────────────────

static constexpr size_t PAGE_SIZE = 4096;
static constexpr size_t NUM_PAGES = 1024;

static uint8_t* get_arena() {
    static uint8_t* arena = nullptr;
    if (!arena) {
        arena = static_cast<uint8_t*>(aligned_alloc(PAGE_SIZE, NUM_PAGES * PAGE_SIZE));
    }
    return arena;
}

alignas(64) static uint8_t g_buddy_storage[sizeof(kernel::memory::buddy_allocator)];

static kernel::memory::buddy_allocator* make_buddy() {
    auto* arena = get_arena();
    ::memset(arena, 0, NUM_PAGES * PAGE_SIZE);
    ::memset(g_buddy_storage, 0, sizeof(g_buddy_storage));
    auto* b = new (g_buddy_storage) kernel::memory::buddy_allocator();
    b->init(reinterpret_cast<uintptr_t>(arena), NUM_PAGES);
    return b;
}

TEST(buddy_init) {
    auto* b = make_buddy();
    ASSERT(b->get_free_pages() > 0);
    ASSERT(b->get_total_pages() == NUM_PAGES);
}

TEST(buddy_alloc_single_page) {
    auto* b = make_buddy();
    size_t before = b->get_free_pages();
    uintptr_t p = b->alloc_page();
    ASSERT(p != 0);
    ASSERT_EQ(b->get_free_pages(), before - 1);
}

TEST(buddy_alloc_free_single) {
    auto* b = make_buddy();
    size_t before = b->get_free_pages();
    uintptr_t p = b->alloc_page();
    ASSERT(p != 0);
    b->free_page(p);
    ASSERT_EQ(b->get_free_pages(), before);
}

TEST(buddy_alloc_many_pages) {
    auto* b = make_buddy();
    size_t count = 0;
    uintptr_t pages[NUM_PAGES];
    while (true) {
        uintptr_t p = b->alloc_page();
        if (!p) break;
        pages[count++] = p;
    }
    ASSERT(count > 0);
    ASSERT_EQ(b->get_free_pages(), (size_t)0);
    for (size_t i = 0; i < count; ++i) b->free_page(pages[i]);
    ASSERT(b->get_free_pages() > 0);
}

TEST(buddy_alloc_order1) {
    auto* b = make_buddy();
    uintptr_t p = b->alloc(1);
    ASSERT(p != 0);
    b->free(p, 1);
}

TEST(buddy_alloc_large_order) {
    auto* b = make_buddy();
    uintptr_t p = b->alloc(5); // 32 pages
    ASSERT(p != 0);
    b->free(p, 5);
}

TEST(buddy_coalescing) {
    auto* b = make_buddy();
    size_t initial = b->get_free_pages();
    uintptr_t p1 = b->alloc(0), p2 = b->alloc(0);
    ASSERT(p1 && p2);
    b->free(p1, 0); b->free(p2, 0);
    ASSERT_EQ(b->get_free_pages(), initial);
}

TEST(buddy_no_overlap) {
    auto* b = make_buddy();
    constexpr int N = 100;
    uintptr_t addrs[N];
    for (int i = 0; i < N; ++i) { addrs[i] = b->alloc_page(); ASSERT(addrs[i]); }
    for (int i = 0; i < N; ++i)
        for (int j = i+1; j < N; ++j)
            ASSERT(addrs[i] != addrs[j]);
    for (int i = 0; i < N; ++i) b->free_page(addrs[i]);
}

TEST(buddy_pages_to_order) {
    using B = kernel::memory::buddy_allocator;
    ASSERT_EQ(B::pages_to_order(1), 0U);
    ASSERT_EQ(B::pages_to_order(2), 1U);
    ASSERT_EQ(B::pages_to_order(3), 2U);
    ASSERT_EQ(B::pages_to_order(4), 2U);
    ASSERT_EQ(B::pages_to_order(5), 3U);
    ASSERT_EQ(B::pages_to_order(1024), 10U);
}

TEST(buddy_oom_returns_zero) {
    auto* b = make_buddy();
    uintptr_t p = b->alloc(11);
    ASSERT_EQ(p, (uintptr_t)0);
}

int main() {
    printf("=== Buddy Allocator Tests ===\n");
    return test_summary();
}
