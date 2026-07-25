// SPDX-License-Identifier: MIT
#include <kernel/ds/list.hpp>
#include <kernel/memory/vma.hpp>
#include <knew.hpp>

namespace kernel::memory {

// RB-tree comparator: compare VMAs by start address
static int vma_cmp(const kernel::rb_node* a, const kernel::rb_node* b) noexcept {
    auto* va = kernel::container_of(const_cast<kernel::rb_node*>(a), &vma::tree_node);
    auto* vb = kernel::container_of(const_cast<kernel::rb_node*>(b), &vma::tree_node);
    if (va->start < vb->start) return -1;
    if (va->start > vb->start) return 1;
    return 0;
}

// For find: key is a vma with start = target address.
// Returns 0 if addr is within [start, end), <0 if below, >0 if above.
static int vma_find_cmp(const kernel::rb_node* key, const kernel::rb_node* node) noexcept {
    auto* k = kernel::container_of(const_cast<kernel::rb_node*>(key), &vma::tree_node);
    auto* n = kernel::container_of(const_cast<kernel::rb_node*>(node), &vma::tree_node);
    if (k->start < n->start) return -1;
    if (k->start >= n->end) return 1;
    return 0; // k->start is within [n->start, n->end)
}

void vma_manager::init() noexcept {
    tree_ = kernel::rb_tree{};
}

vma* vma_manager::insert(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, int fd, long offset) noexcept {
    auto* v = new vma();
    if (!v) return nullptr;

    v->start = start;
    v->end = end;
    v->prot = prot;
    v->flags = flags;
    v->fd = fd;
    v->file_offset = offset;
    v->tree_node.init();

    tree_.insert(&v->tree_node, vma_cmp);
    return v;
}

vma* vma_manager::find(uintptr_t addr) const noexcept {
    vma key{};
    key.start = addr;
    key.tree_node.init();

    auto* node = tree_.find_containing(&key.tree_node, vma_find_cmp);
    if (!node) return nullptr;
    return kernel::container_of(node, &vma::tree_node);
}

void vma_manager::remove(uintptr_t start, size_t length) noexcept {
    uintptr_t end = start + length;

    // Find and remove all VMAs that overlap [start, end)
    while (true) {
        vma key{};
        key.start = start;
        key.tree_node.init();

        auto* node = tree_.find_containing(&key.tree_node, vma_find_cmp);
        if (!node) break;

        auto* v = kernel::container_of(node, &vma::tree_node);

        if (v->start >= start && v->end <= end) {
            // VMA is fully contained — remove entirely
            tree_.remove(&v->tree_node);
            delete v;
        } else if (v->start < start && v->end > end) {
            // VMA spans both sides — split into two
            auto* right = new vma();
            right->start = end;
            right->end = v->end;
            right->prot = v->prot;
            right->flags = v->flags;
            right->fd = v->fd;
            right->file_offset = v->file_offset + static_cast<long>(end - v->start);
            right->tree_node.init();

            v->end = start; // Shrink left part
            tree_.insert(&right->tree_node, vma_cmp);
            break;
        } else if (v->start < start) {
            // Overlap on the right — shrink
            v->end = start;
        } else {
            // Overlap on the left — shrink
            v->file_offset += static_cast<long>(end - v->start);
            v->start = end;
        }

        // Keep searching for more overlapping VMAs
        if (start >= end) break;
    }
}

void vma_manager::destroy_all() noexcept {
    // Walk the tree and delete all VMAs
    while (!tree_.empty()) {
        auto* node = tree_.first();
        tree_.remove(node);
        auto* v = kernel::container_of(node, &vma::tree_node);
        delete v;
    }
}

} // namespace kernel::memory
