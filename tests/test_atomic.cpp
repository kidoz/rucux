// SPDX-License-Identifier: MIT
// Host-compiled test for kernel::atomic<T>
//
// We directly include the kernel header — it uses __atomic builtins
// which work on host compilers (GCC/Clang) without any kernel runtime.

#include "test_harness.hpp"
#include "../src/include/kernel/sync/atomic.hpp"

TEST(atomic_default_init) {
    kernel::atomic<int> a;
    ASSERT_EQ(a.load(), 0);
}

TEST(atomic_explicit_init) {
    kernel::atomic<int> a{42};
    ASSERT_EQ(a.load(), 42);
}

TEST(atomic_store_load) {
    kernel::atomic<int> a{0};
    a.store(100);
    ASSERT_EQ(a.load(), 100);
}

TEST(atomic_exchange) {
    kernel::atomic<int> a{10};
    int old = a.exchange(20);
    ASSERT_EQ(old, 10);
    ASSERT_EQ(a.load(), 20);
}

TEST(atomic_fetch_add) {
    kernel::atomic<int> a{5};
    int old = a.fetch_add(3);
    ASSERT_EQ(old, 5);
    ASSERT_EQ(a.load(), 8);
}

TEST(atomic_fetch_sub) {
    kernel::atomic<int> a{10};
    int old = a.fetch_sub(3);
    ASSERT_EQ(old, 10);
    ASSERT_EQ(a.load(), 7);
}

TEST(atomic_fetch_and) {
    kernel::atomic<uint32_t> a{0xFF};
    uint32_t old = a.fetch_and(0x0F);
    ASSERT_EQ(old, 0xFFu);
    ASSERT_EQ(a.load(), 0x0Fu);
}

TEST(atomic_fetch_or) {
    kernel::atomic<uint32_t> a{0x0F};
    uint32_t old = a.fetch_or(0xF0);
    ASSERT_EQ(old, 0x0Fu);
    ASSERT_EQ(a.load(), 0xFFu);
}

TEST(atomic_compare_exchange_success) {
    kernel::atomic<int> a{42};
    int expected = 42;
    bool ok = a.compare_exchange_strong(expected, 99);
    ASSERT(ok);
    ASSERT_EQ(a.load(), 99);
    ASSERT_EQ(expected, 42);
}

TEST(atomic_compare_exchange_failure) {
    kernel::atomic<int> a{42};
    int expected = 0; // Wrong
    bool ok = a.compare_exchange_strong(expected, 99);
    ASSERT(!ok);
    ASSERT_EQ(a.load(), 42);
    ASSERT_EQ(expected, 42); // Updated to actual value
}

TEST(atomic_memory_orders) {
    kernel::atomic<int> a{0};
    a.store(1, kernel::relaxed);
    ASSERT_EQ(a.load(kernel::relaxed), 1);
    a.store(2, kernel::release);
    ASSERT_EQ(a.load(kernel::acquire), 2);
}

TEST(atomic_uint16) {
    kernel::atomic<uint16_t> a{0};
    a.fetch_add(1);
    a.fetch_add(1);
    ASSERT_EQ(a.load(), 2);
}

TEST(atomic_uint16_wrap) {
    kernel::atomic<uint16_t> a{0xFFFF};
    a.fetch_add(1);
    ASSERT_EQ(a.load(), 0);
}

int main() {
    printf("=== Atomic Tests ===\n");
    return test_summary();
}
