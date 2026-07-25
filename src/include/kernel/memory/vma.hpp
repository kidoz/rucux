// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/ds/rbtree.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

// Virtual Memory Area — tracks a contiguous mapped region in an address space.
struct vma {
    rb_node tree_node; // RB-tree link (sorted by start address)
    uintptr_t start;   // Start virtual address (page-aligned)
    uintptr_t end;     // End virtual address (exclusive, page-aligned)
    uint32_t prot;     // PROT_READ | PROT_WRITE | PROT_EXEC
    uint32_t flags;    // MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS
    int fd;            // Backing file descriptor (-1 for anonymous)
    long file_offset;  // Offset into backing file
};

// Per-address-space VMA manager.
class vma_manager {
public:
    void init() noexcept;

    // Insert a new VMA. Returns the VMA or nullptr on overlap/OOM.
    vma* insert(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, int fd, long offset) noexcept;

    // Find the VMA containing the given address, or nullptr.
    vma* find(uintptr_t addr) const noexcept;

    // Remove and free the VMA covering [start, start+length).
    // Handles partial overlaps by splitting VMAs.
    void remove(uintptr_t start, size_t length) noexcept;

    // Iterate all VMAs (for cleanup on address space destruction)
    void destroy_all() noexcept;

private:
    kernel::rb_tree tree_;
};

} // namespace kernel::memory
