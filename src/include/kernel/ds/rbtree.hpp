// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel {

// Intrusive red-black tree node.
// Embed in your struct and provide a comparator.
enum class rb_color : uint8_t { RED, BLACK };

struct rb_node {
    rb_node* parent;
    rb_node* left;
    rb_node* right;
    rb_color color;

    constexpr void init() noexcept {
        parent = left = right = nullptr;
        color = rb_color::RED;
    }
};

// Intrusive red-black tree.
// T must have an rb_node member. User provides comparison via a callback.
class rb_tree {
public:
    using cmp_fn = int (*)(const rb_node* a, const rb_node* b);

    constexpr rb_tree() noexcept
        : root_{nullptr} {}

    void insert(rb_node* node, cmp_fn cmp) noexcept;
    void remove(rb_node* node) noexcept;

    // Find exact match (cmp returns 0)
    rb_node* find(const rb_node* key, cmp_fn cmp) const noexcept;

    // Find the node whose range contains the key, or nullptr.
    // Used for VMA lookup: finds the VMA whose [start, end) contains an address.
    // key_cmp should return <0 if key is below node's range, >0 if above, 0 if inside.
    rb_node* find_containing(const rb_node* key, cmp_fn key_cmp) const noexcept;

    rb_node* first() const noexcept; // Leftmost (smallest)
    rb_node* last() const noexcept;  // Rightmost (largest)
    bool empty() const noexcept { return root_ == nullptr; }

    rb_node* root() const noexcept { return root_; }

private:
    void rotate_left(rb_node* node) noexcept;
    void rotate_right(rb_node* node) noexcept;
    void insert_fixup(rb_node* node) noexcept;
    void remove_fixup(rb_node* node, rb_node* parent) noexcept;
    void transplant(rb_node* u, rb_node* v) noexcept;

    rb_node* root_;
};

} // namespace kernel
