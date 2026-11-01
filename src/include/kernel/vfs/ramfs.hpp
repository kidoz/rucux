// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/vfs/vfs.hpp>
#include <lib/stddef.hpp>

namespace kernel::vfs {

class ramfs {
public:
    static vfs_node* create_root() noexcept;
    static vfs_node* create_file(vfs_node* parent, const char* name, const uint8_t* content, size_t size) noexcept;
    static vfs_node* create_directory(vfs_node* parent, const char* name) noexcept;
    static void attach_node(vfs_node* parent, vfs_node* child) noexcept;

    static long read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept;
    static long write(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept;
    static long append(vfs_node* node, size_t size, const void* buffer, size_t& end_offset) noexcept;
    static int truncate(vfs_node* node, size_t length) noexcept;
    static int fsync(vfs_node* node) noexcept;
    static vfs_node* readdir(vfs_node* node, size_t index) noexcept;
    static int lookup(vfs_node* node, const char* name, vfs_node*& result) noexcept;

private:
    struct ramfs_node {
        uint8_t* content;
        size_t size;
        ramfs_node* next; // Linked list of children in same directory
        ramfs_node* first_child;
    };
};

} // namespace kernel::vfs
