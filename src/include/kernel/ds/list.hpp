// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel {

// Intrusive doubly-linked circular list (Linux-style list_head).
// Embed list_node in your struct, then use container_of to get back to the parent.
// The list head is a sentinel node — an empty list points to itself.

struct list_node {
    list_node* next;
    list_node* prev;

    constexpr void init() noexcept {
        next = this;
        prev = this;
    }

    bool empty() const noexcept { return next == this; }

    void insert_after(list_node* node) noexcept {
        node->next = next;
        node->prev = this;
        next->prev = node;
        next = node;
    }

    void insert_before(list_node* node) noexcept {
        node->next = this;
        node->prev = prev;
        prev->next = node;
        prev = node;
    }

    void remove() noexcept {
        prev->next = next;
        next->prev = prev;
        next = this;
        prev = this;
    }

    // Push to front/back of list headed by this sentinel
    void push_front(list_node* node) noexcept { insert_after(node); }
    void push_back(list_node* node) noexcept { insert_before(node); }
};

// Get the containing struct from a list_node member pointer.
// Usage: auto* obj = container_of(node_ptr, &MyStruct::list_member);
template <typename T, typename M>
inline T* container_of(M* ptr, M T::* member) noexcept {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) -
                                reinterpret_cast<uintptr_t>(&(static_cast<T*>(nullptr)->*member)));
}

} // namespace kernel
