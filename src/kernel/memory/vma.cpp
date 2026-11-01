// SPDX-License-Identifier: MIT
#include <kernel/ds/list.hpp>
#include <kernel/memory/vma.hpp>
#include <knew.hpp>

namespace kernel::memory {

namespace {

vma* node_vma(kernel::rb_node* node) noexcept {
    return kernel::container_of(node, &vma::tree_node);
}

int vma_cmp(const kernel::rb_node* a, const kernel::rb_node* b) noexcept {
    auto* va = node_vma(const_cast<kernel::rb_node*>(a));
    auto* vb = node_vma(const_cast<kernel::rb_node*>(b));
    if (va->start < vb->start) return -1;
    if (va->start > vb->start) return 1;
    return 0;
}

// key->start is the address being looked up.
int vma_find_cmp(const kernel::rb_node* key, const kernel::rb_node* node) noexcept {
    auto* k = node_vma(const_cast<kernel::rb_node*>(key));
    auto* n = node_vma(const_cast<kernel::rb_node*>(node));
    if (k->start < n->start) return -1;
    if (k->start >= n->end) return 1;
    return 0;
}

} // namespace

void vma_manager::init() noexcept {
    tree_ = kernel::rb_tree{};
}

vma* vma_manager::advance(vma* region) const noexcept {
    if (!region) return nullptr;
    auto* node = tree_.next(&region->tree_node);
    return node ? node_vma(node) : nullptr;
}

vma* vma_manager::first_overlap(uintptr_t start, uintptr_t end) const noexcept {
    if (start >= end) return nullptr;
    for (auto* node = tree_.first(); node; node = tree_.next(node)) {
        vma* region = node_vma(node);
        if (region->start >= end) return nullptr;
        if (region->end > start) return region;
    }
    return nullptr;
}

vma* vma_manager::split(vma* region, uintptr_t at) noexcept {
    if (!region || at <= region->start || at >= region->end) return region;
    auto* right = new vma();
    if (!right) return nullptr;
    right->start = at;
    right->end = region->end;
    right->prot = region->prot;
    right->flags = region->flags;
    right->fd = region->fd;
    if (auto* node = region->backing.get()) node->retain();
    right->backing.reset(region->backing.get());
    right->file_offset = region->file_offset + static_cast<long>(at - region->start);
    right->tree_node.init();
    region->end = at;
    tree_.insert(&right->tree_node, vma_cmp);
    return right;
}

vma* vma_manager::insert(uintptr_t start, uintptr_t end, uint32_t prot, uint32_t flags, int fd, long offset,
                         vfs::vfs_node* backing) noexcept {
    if (start >= end || first_overlap(start, end)) return nullptr;
    auto* region = new vma();
    if (!region) return nullptr;

    region->start = start;
    region->end = end;
    region->prot = prot;
    region->flags = flags;
    region->fd = fd;
    region->file_offset = offset;
    if (backing) backing->retain();
    region->backing.reset(backing);
    region->tree_node.init();

    tree_.insert(&region->tree_node, vma_cmp);
    return region;
}

vma* vma_manager::find(uintptr_t addr) const noexcept {
    vma key{};
    key.start = addr;
    key.tree_node.init();

    auto* node = tree_.find_containing(&key.tree_node, vma_find_cmp);
    if (!node) return nullptr;
    return node_vma(node);
}

bool vma_manager::covers(uintptr_t start, uintptr_t end) const noexcept {
    if (start >= end) return false;
    uintptr_t cursor = start;
    while (cursor < end) {
        vma* region = find(cursor);
        if (!region || region->end <= cursor) return false;
        cursor = region->end;
    }
    return true;
}

bool vma_manager::isolate(uintptr_t start, uintptr_t end) noexcept {
    if (start >= end) return false;
    vma* first = first_overlap(start, end);
    if (!first) return true;
    if (first->start < start && !split(first, start)) return false;
    vma* last = find(end - 1);
    if (last && last->end > end && !split(last, end)) return false;
    return true;
}

bool vma_manager::set_prot(uintptr_t start, uintptr_t end, uint32_t prot) noexcept {
    if (!covers(start, end)) return false;
    vma* region = find(start);
    if (!region) return false;
    if (region->start < start) {
        region = split(region, start);
        if (!region) return false;
    }

    vma* crossing = nullptr;
    for (vma* cur = region; cur && cur->start < end; cur = advance(cur)) {
        if (cur->end > end) {
            crossing = cur;
            break;
        }
    }
    if (crossing && !split(crossing, end)) return false;

    for (vma* cur = region; cur && cur->start < end; cur = advance(cur))
        cur->prot = prot;
    return true;
}

bool vma_manager::remove(uintptr_t start, size_t length) noexcept {
    if (length == 0) return true;
    if (start + length < start) return false;
    uintptr_t end = start + length;

    vma* span = nullptr;
    for (auto* node = tree_.first(); node; node = tree_.next(node)) {
        vma* region = node_vma(node);
        if (region->start >= end) break;
        if (region->start < start && region->end > end) {
            span = region;
            break;
        }
    }

    vma* right = nullptr;
    if (span) {
        right = new vma();
        if (!right) return false;
        right->start = end;
        right->end = span->end;
        right->prot = span->prot;
        right->flags = span->flags;
        right->fd = span->fd;
        if (auto* node = span->backing.get()) node->retain();
        right->backing.reset(span->backing.get());
        right->file_offset = span->file_offset + static_cast<long>(end - span->start);
        right->tree_node.init();
    }

    while (vma* region = first_overlap(start, end)) {
        if (region->start >= start && region->end <= end) {
            tree_.remove(&region->tree_node);
            delete region;
            continue;
        }
        if (region->start < start && region->end > end) {
            region->end = start;
            tree_.insert(&right->tree_node, vma_cmp);
            right = nullptr;
            continue;
        }
        if (region->start < start) {
            region->end = start;
            continue;
        }
        region->file_offset += static_cast<long>(end - region->start);
        region->start = end;
    }

    delete right;
    return true;
}

bool vma_manager::owns_frame(uintptr_t address) const noexcept {
    const auto* region = find(address);
    return !region || (region->flags & VMA_OWNS_FRAMES) != 0;
}

void vma_manager::destroy_all() noexcept {
    while (!tree_.empty()) {
        auto* node = tree_.first();
        tree_.remove(node);
        delete node_vma(node);
    }
}

address_space* create_mm() noexcept {
    auto* mm = new address_space();
    if (!mm) return nullptr;
    mm->vmas.init();
#if defined(__x86_64__)
    mm->next_mmap = 0xA000000000ULL;
#elif defined(__aarch64__)
    mm->next_mmap = 0x2000000000ULL;
#else
    mm->next_mmap = 0x80000000UL;
#endif
    return mm;
}

void destroy_mm(address_space* mm) noexcept {
    if (!mm) return;
    mm->vmas.destroy_all();
    delete mm;
}

} // namespace kernel::memory
