// SPDX-License-Identifier: MIT
#include <kernel/ds/rbtree.hpp>

namespace kernel {

void rb_tree::rotate_left(rb_node* x) noexcept {
    rb_node* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent)
        root_ = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
}

void rb_tree::rotate_right(rb_node* x) noexcept {
    rb_node* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (!x->parent)
        root_ = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;
    y->right = x;
    x->parent = y;
}

void rb_tree::insert_fixup(rb_node* z) noexcept {
    while (z->parent && z->parent->color == rb_color::RED) {
        if (z->parent == z->parent->parent->left) {
            rb_node* y = z->parent->parent->right;
            if (y && y->color == rb_color::RED) {
                z->parent->color = rb_color::BLACK;
                y->color = rb_color::BLACK;
                z->parent->parent->color = rb_color::RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rotate_left(z);
                }
                z->parent->color = rb_color::BLACK;
                z->parent->parent->color = rb_color::RED;
                rotate_right(z->parent->parent);
            }
        } else {
            rb_node* y = z->parent->parent->left;
            if (y && y->color == rb_color::RED) {
                z->parent->color = rb_color::BLACK;
                y->color = rb_color::BLACK;
                z->parent->parent->color = rb_color::RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rotate_right(z);
                }
                z->parent->color = rb_color::BLACK;
                z->parent->parent->color = rb_color::RED;
                rotate_left(z->parent->parent);
            }
        }
    }
    root_->color = rb_color::BLACK;
}

void rb_tree::insert(rb_node* z, cmp_fn cmp) noexcept {
    z->left = z->right = nullptr;
    z->color = rb_color::RED;

    rb_node* y = nullptr;
    rb_node* x = root_;
    while (x) {
        y = x;
        x = (cmp(z, x) < 0) ? x->left : x->right;
    }
    z->parent = y;
    if (!y)
        root_ = z;
    else if (cmp(z, y) < 0)
        y->left = z;
    else
        y->right = z;

    insert_fixup(z);
}

void rb_tree::transplant(rb_node* u, rb_node* v) noexcept {
    if (!u->parent)
        root_ = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;
    if (v) v->parent = u->parent;
}

static rb_node* tree_minimum(rb_node* x) noexcept {
    while (x->left)
        x = x->left;
    return x;
}

void rb_tree::remove_fixup(rb_node* x, rb_node* x_parent) noexcept {
    while (x != root_ && (!x || x->color == rb_color::BLACK)) {
        if (x == x_parent->left) {
            rb_node* w = x_parent->right;
            if (w && w->color == rb_color::RED) {
                w->color = rb_color::BLACK;
                x_parent->color = rb_color::RED;
                rotate_left(x_parent);
                w = x_parent->right;
            }
            if ((!w->left || w->left->color == rb_color::BLACK) && (!w->right || w->right->color == rb_color::BLACK)) {
                w->color = rb_color::RED;
                x = x_parent;
                x_parent = x->parent;
            } else {
                if (!w->right || w->right->color == rb_color::BLACK) {
                    if (w->left) w->left->color = rb_color::BLACK;
                    w->color = rb_color::RED;
                    rotate_right(w);
                    w = x_parent->right;
                }
                w->color = x_parent->color;
                x_parent->color = rb_color::BLACK;
                if (w->right) w->right->color = rb_color::BLACK;
                rotate_left(x_parent);
                x = root_;
            }
        } else {
            rb_node* w = x_parent->left;
            if (w && w->color == rb_color::RED) {
                w->color = rb_color::BLACK;
                x_parent->color = rb_color::RED;
                rotate_right(x_parent);
                w = x_parent->left;
            }
            if ((!w->right || w->right->color == rb_color::BLACK) && (!w->left || w->left->color == rb_color::BLACK)) {
                w->color = rb_color::RED;
                x = x_parent;
                x_parent = x->parent;
            } else {
                if (!w->left || w->left->color == rb_color::BLACK) {
                    if (w->right) w->right->color = rb_color::BLACK;
                    w->color = rb_color::RED;
                    rotate_left(w);
                    w = x_parent->left;
                }
                w->color = x_parent->color;
                x_parent->color = rb_color::BLACK;
                if (w->left) w->left->color = rb_color::BLACK;
                rotate_right(x_parent);
                x = root_;
            }
        }
    }
    if (x) x->color = rb_color::BLACK;
}

void rb_tree::remove(rb_node* z) noexcept {
    rb_node* y = z;
    rb_color y_original = y->color;
    rb_node* x = nullptr;
    rb_node* x_parent = nullptr;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;
        transplant(z, z->right);
    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;
        transplant(z, z->left);
    } else {
        y = tree_minimum(z->right);
        y_original = y->color;
        x = y->right;
        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            transplant(y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        transplant(z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    if (y_original == rb_color::BLACK) remove_fixup(x, x_parent);
}

rb_node* rb_tree::find(const rb_node* key, cmp_fn cmp) const noexcept {
    rb_node* x = root_;
    while (x) {
        int c = cmp(key, x);
        if (c == 0) return x;
        x = (c < 0) ? x->left : x->right;
    }
    return nullptr;
}

rb_node* rb_tree::find_containing(const rb_node* key, cmp_fn key_cmp) const noexcept {
    rb_node* x = root_;
    while (x) {
        int c = key_cmp(key, x);
        if (c == 0) return x;
        x = (c < 0) ? x->left : x->right;
    }
    return nullptr;
}

rb_node* rb_tree::first() const noexcept {
    if (!root_) return nullptr;
    return tree_minimum(root_);
}

rb_node* rb_tree::last() const noexcept {
    if (!root_) return nullptr;
    rb_node* x = root_;
    while (x->right)
        x = x->right;
    return x;
}

} // namespace kernel
