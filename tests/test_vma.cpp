// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <cstdlib>
#include <kernel/memory/mmap.hpp>
#include <kernel/memory/vma.hpp>

namespace {

struct AllocationHeader {
    size_t size;
};

} // namespace

void* operator new(size_t size) {
    auto* header = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + size));
    if (!header) std::abort();
    header->size = size;
    return header + 1;
}

void* operator new[](size_t size) {
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    std::free(static_cast<AllocationHeader*>(ptr) - 1);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    ::operator delete(ptr);
}

namespace {

using kernel::memory::page_flags;
using kernel::memory::vma_manager;

constexpr uint32_t k_read = 1;
constexpr uint32_t k_write = 2;
constexpr uint32_t k_exec = 4;

vma_manager make_manager() {
    vma_manager manager;
    manager.init();
    return manager;
}

bool has_flag(page_flags flags, page_flags bit) {
    const auto raw = static_cast<uint64_t>(flags);
    return (raw & static_cast<uint64_t>(bit)) != 0;
}

TEST(overlap_is_rejected) {
    auto manager = make_manager();
    ASSERT(manager.insert(0x1000, 0x3000, k_read, 0, -1, 0) != nullptr);
    ASSERT(manager.insert(0x2000, 0x4000, k_read, 0, -1, 0) == nullptr);
    ASSERT(manager.insert(0x0000, 0x1000, k_read, 0, -1, 0) != nullptr);
    ASSERT(manager.insert(0x3000, 0x4000, k_read, 0, -1, 0) != nullptr);
    manager.destroy_all();
}

TEST(remove_reaches_areas_after_a_hole) {
    auto manager = make_manager();
    ASSERT(manager.insert(0x3000, 0x5000, k_read, kernel::memory::VMA_OWNS_FRAMES, 4, 10) != nullptr);
    ASSERT(manager.insert(0x6000, 0x8000, k_write, 0, 5, 20) != nullptr);
    ASSERT(manager.remove(0x2000, 0x5000));

    ASSERT(manager.find(0x3000) == nullptr);
    ASSERT(manager.find(0x6000) == nullptr);
    auto* tail = manager.find(0x7000);
    ASSERT(tail != nullptr);
    ASSERT_EQ(tail->start, static_cast<uintptr_t>(0x7000));
    ASSERT_EQ(tail->end, static_cast<uintptr_t>(0x8000));
    ASSERT_EQ(tail->prot, k_write);
    ASSERT_EQ(tail->file_offset, static_cast<long>(20 + 0x1000));
    manager.destroy_all();
}

TEST(remove_splits_a_spanning_area) {
    auto manager = make_manager();
    ASSERT(manager.insert(0x1000, 0x5000, k_read, kernel::memory::VMA_OWNS_FRAMES, 3, 10) != nullptr);
    ASSERT(manager.remove(0x2000, 0x1000));

    auto* left = manager.find(0x1000);
    auto* right = manager.find(0x3000);
    ASSERT(left != nullptr);
    ASSERT(right != nullptr);
    ASSERT(manager.find(0x2000) == nullptr);
    ASSERT_EQ(left->end, static_cast<uintptr_t>(0x2000));
    ASSERT_EQ(left->file_offset, static_cast<long>(10));
    ASSERT_EQ(left->flags, kernel::memory::VMA_OWNS_FRAMES);
    ASSERT_EQ(right->start, static_cast<uintptr_t>(0x3000));
    ASSERT_EQ(right->end, static_cast<uintptr_t>(0x5000));
    ASSERT_EQ(right->file_offset, static_cast<long>(10 + 0x2000));
    ASSERT_EQ(right->fd, 3);
    manager.destroy_all();
}

TEST(protect_splits_without_touching_neighbors) {
    auto manager = make_manager();
    ASSERT(manager.insert(0x0000, 0x3000, k_read, kernel::memory::VMA_OWNS_FRAMES, 4, 50) != nullptr);
    ASSERT(manager.set_prot(0x1000, 0x2000, k_exec));

    auto* left = manager.find(0x0000);
    auto* middle = manager.find(0x1000);
    auto* right = manager.find(0x2000);
    ASSERT(left != nullptr && middle != nullptr && right != nullptr);
    ASSERT_EQ(left->end, static_cast<uintptr_t>(0x1000));
    ASSERT_EQ(left->prot, k_read);
    ASSERT_EQ(left->file_offset, static_cast<long>(50));
    ASSERT_EQ(middle->start, static_cast<uintptr_t>(0x1000));
    ASSERT_EQ(middle->end, static_cast<uintptr_t>(0x2000));
    ASSERT_EQ(middle->prot, k_exec);
    ASSERT_EQ(middle->flags, kernel::memory::VMA_OWNS_FRAMES);
    ASSERT_EQ(middle->file_offset, static_cast<long>(50 + 0x1000));
    ASSERT_EQ(right->start, static_cast<uintptr_t>(0x2000));
    ASSERT_EQ(right->end, static_cast<uintptr_t>(0x3000));
    ASSERT_EQ(right->prot, k_read);
    ASSERT_EQ(right->file_offset, static_cast<long>(50 + 0x2000));
    manager.destroy_all();
}

TEST(protect_rejects_a_gap) {
    auto manager = make_manager();
    ASSERT(manager.insert(0x0000, 0x1000, k_read, 0, -1, 0) != nullptr);
    ASSERT(manager.insert(0x2000, 0x3000, k_read, 0, -1, 0) != nullptr);
    ASSERT(!manager.covers(0x0000, 0x3000));
    ASSERT(!manager.set_prot(0x0000, 0x3000, k_write));
    ASSERT_EQ(manager.find(0x0000)->prot, k_read);
    ASSERT_EQ(manager.find(0x2000)->prot, k_read);
    ASSERT_EQ(manager.find(0x0000)->end, static_cast<uintptr_t>(0x1000));
    manager.destroy_all();
}

TEST(fault_permissions_follow_protection) {
    const auto read_only = kernel::memory::flags_for_prot(k_read);
    const auto writable = kernel::memory::flags_for_prot(k_read | k_write);
    const auto executable = kernel::memory::flags_for_prot(k_read | k_exec);

    ASSERT(has_flag(read_only, page_flags::PRESENT));
    ASSERT(has_flag(read_only, page_flags::USER));
    ASSERT(has_flag(read_only, page_flags::NO_EXECUTE));
    ASSERT(!has_flag(read_only, page_flags::WRITABLE));

    ASSERT(has_flag(writable, page_flags::WRITABLE));
    ASSERT(has_flag(writable, page_flags::NO_EXECUTE));
    ASSERT(!has_flag(executable, page_flags::NO_EXECUTE));
    ASSERT(!has_flag(executable, page_flags::WRITABLE));
}

} // namespace

int main() {
    return rucux_test::run_all_tests("Per-process VMAs");
}
