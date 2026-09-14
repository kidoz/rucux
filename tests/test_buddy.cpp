// SPDX-License-Identifier: MIT
// Exercise the production allocator; hosted IRQ helpers are no-ops.
#include "test_harness.hpp"
#include <kernel/memory/buddy.hpp>

#include <cstdint>
#include <cstring>
#include <new>

// Only console I/O is replaced. Allocation and locking use production code.
namespace kernel {
void kputc(char) noexcept {}
void kwrite(const char*) noexcept {}
namespace console {
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
} // namespace console
} // namespace kernel

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
    for (size_t i = 0; i < count; ++i)
        b->free_page(pages[i]);
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
    b->free(p1, 0);
    b->free(p2, 0);
    ASSERT_EQ(b->get_free_pages(), initial);
}

TEST(buddy_no_overlap) {
    auto* b = make_buddy();
    constexpr int N = 100;
    uintptr_t addrs[N];
    for (int i = 0; i < N; ++i) {
        addrs[i] = b->alloc_page();
        ASSERT(addrs[i]);
    }
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j)
            ASSERT(addrs[i] != addrs[j]);
    for (int i = 0; i < N; ++i)
        b->free_page(addrs[i]);
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

TEST(buddy_reallocation_preserves_live_pages) {
    auto* b = make_buddy();
    uintptr_t pages[128]{};
    for (unsigned i = 0; i < 128; ++i) {
        pages[i] = b->alloc_page();
        ASSERT(pages[i]);
        memset(reinterpret_cast<void*>(pages[i]), static_cast<int>(i + 1), PAGE_SIZE);
    }
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        for (unsigned i = 0; i < 128; i += 2)
            b->free_page(pages[i]);
        for (unsigned i = 0; i < 128; i += 2) {
            pages[i] = b->alloc_page();
            ASSERT(pages[i]);
            memset(reinterpret_cast<void*>(pages[i]), static_cast<int>(i + 1), PAGE_SIZE);
        }
        for (unsigned i = 0; i < 128; ++i) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(pages[i]);
            for (size_t j = 0; j < PAGE_SIZE; ++j)
                ASSERT_EQ(bytes[j], static_cast<unsigned char>(i + 1));
            for (unsigned j = 0; j < i; ++j)
                ASSERT(pages[i] != pages[j]);
        }
    }
    for (auto address : pages)
        b->free_page(address);
}

int main() {
    return RUN_ALL_TESTS("Buddy Allocator Tests");
}
