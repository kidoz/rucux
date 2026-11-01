// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/ds/rbtree.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

// Set on VMAs whose frames this address space allocated. File ops->mmap
// frames stay clear so munmap drops the translation without freeing them.
inline constexpr uint32_t VMA_OWNS_FRAMES = 0x80000000u;
// Eager population owns this range until success or rollback. Overlapping
// unmap/protect operations must wait for the caller to retry after completion.
inline constexpr uint32_t VMA_POPULATING = 0x40000000u;

// Virtual Memory Area — tracks a contiguous mapped region in an address space.
struct vma {
    rb_node tree_node; // RB-tree link (sorted by start address)
    uintptr_t start;   // Start virtual address (page-aligned)
    uintptr_t end;     // End virtual address (exclusive, page-aligned)
    uint32_t prot;     // PROT_READ | PROT_WRITE | PROT_EXEC
    uint32_t flags;    // MAP_* plus VMA_OWNS_FRAMES
    int fd;            // Backing file descriptor (-1 for anonymous)
    long file_offset;  // Offset into backing file
};

// Mappings that belong to one page-table root. Clone siblings share one object.
class vma_manager {
public:
    void init() noexcept;

    // Insert a new VMA. Returns the VMA or nullptr on overlap/OOM.
    vma* insert(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, int fd, long offset) noexcept;

    // Find the VMA containing the given address, or nullptr.
    vma* find(uintptr_t addr) const noexcept;

    // True when every byte of [start, end) belongs to some VMA.
    bool covers(uintptr_t start, uintptr_t end) const noexcept;

    // Split edges so each VMA that meets [start, end) lies fully inside it.
    // A failed split leaves protections unchanged.
    bool isolate(uintptr_t start, uintptr_t end) noexcept;

    // Change protection on a fully covered range, splitting partial VMAs.
    bool set_prot(uintptr_t start, uintptr_t end, uint32_t prot) noexcept;

    // Remove VMAs covering [start, start+length), splitting partial overlaps.
    // Returns false when a split cannot be allocated; earlier VMAs may already
    // have been removed.
    bool remove(uintptr_t start, size_t length) noexcept;

    // Delete VMA nodes only. Frames belong to the page tables.
    void destroy_all() noexcept;

private:
    vma* first_overlap(uintptr_t start, uintptr_t end) const noexcept;
    vma* split(vma* region, uintptr_t at) noexcept;
    vma* advance(vma* region) const noexcept;

    kernel::rb_tree tree_;
};

struct address_space {
    vma_manager vmas{};
    uintptr_t next_mmap = 0;
    irq_spinlock lock{};
};

address_space* create_mm() noexcept;
void destroy_mm(address_space* mm) noexcept;

} // namespace kernel::memory
