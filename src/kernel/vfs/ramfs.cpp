// SPDX-License-Identifier: MIT
#include <kernel/vfs/ramfs.hpp>
#include <lib/string.hpp>
#include <knew.hpp>

namespace kernel::vfs {

struct ramfs_node_internal {
    vfs_node* vnode;
    uint8_t* data;
    size_t size;
    ramfs_node_internal* next;        // Sibling
    ramfs_node_internal* first_child; // For directories
};

static vfs_ops g_ramfs_ops = {.read = ramfs::read,
                              .write = ramfs::write,
                              .open = nullptr,
                              .close = nullptr,
                              .ioctl = nullptr,
                              .readdir = ramfs::readdir,
                              .finddir = ramfs::finddir,
                              .mmap = nullptr,
                              .poll = nullptr};

static char* kstrdup(const char* s) noexcept {
    size_t len = lib::strlen(s);
    char* dup = new char[len + 1];
    lib::memcpy(dup, s, len + 1);
    return dup;
}

static ramfs_node_internal* create_internal(const char* name, file_type type) noexcept {
    vfs_node* v = new vfs_node();
    v->name = kstrdup(name);
    v->name_hash = vfs_node::hash_name(name);
    v->type = type;
    v->ops = &g_ramfs_ops;

    ramfs_node_internal* internal = new ramfs_node_internal();
    internal->vnode = v;
    internal->data = nullptr;
    internal->size = 0;
    internal->next = nullptr;
    internal->first_child = nullptr;

    v->ptr = reinterpret_cast<vfs_node*>(internal);
    return internal;
}

vfs_node* ramfs::create_root() noexcept {
    return create_internal("/", file_type::DIRECTORY)->vnode;
}

vfs_node* ramfs::create_file(vfs_node* parent, const char* name, const uint8_t* content, size_t size) noexcept {
    ramfs_node_internal* p_int = reinterpret_cast<ramfs_node_internal*>(parent->ptr);
    ramfs_node_internal* f_int = create_internal(name, file_type::REGULAR);

    f_int->vnode->length = size;
    f_int->size = size;
    f_int->data = new uint8_t[size];
    lib::memcpy(f_int->data, content, size);

    // Link to parent
    if (!p_int->first_child) {
        p_int->first_child = f_int;
    } else {
        ramfs_node_internal* cur = p_int->first_child;
        while (cur->next)
            cur = cur->next;
        cur->next = f_int;
    }
    return f_int->vnode;
}

vfs_node* ramfs::create_directory(vfs_node* parent, const char* name) noexcept {
    ramfs_node_internal* p_int = reinterpret_cast<ramfs_node_internal*>(parent->ptr);
    ramfs_node_internal* d_int = create_internal(name, file_type::DIRECTORY);

    if (!p_int->first_child) {
        p_int->first_child = d_int;
    } else {
        ramfs_node_internal* cur = p_int->first_child;
        while (cur->next)
            cur = cur->next;
        cur->next = d_int;
    }
    return d_int->vnode;
}

void ramfs::attach_node(vfs_node* parent, vfs_node* child) noexcept {
    ramfs_node_internal* p_int = reinterpret_cast<ramfs_node_internal*>(parent->ptr);

    ramfs_node_internal* c_int = new ramfs_node_internal();
    c_int->vnode = child;
    c_int->data = nullptr;
    c_int->size = 0;
    c_int->next = nullptr;
    c_int->first_child = nullptr;

    if (!p_int->first_child) {
        p_int->first_child = c_int;
    } else {
        ramfs_node_internal* cur = p_int->first_child;
        while (cur->next)
            cur = cur->next;
        cur->next = c_int;
    }
}

size_t ramfs::read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept {
    ramfs_node_internal* internal = reinterpret_cast<ramfs_node_internal*>(node->ptr);
    if (!internal->data || offset >= internal->size) return 0;
    if (offset + size > internal->size) size = internal->size - offset;
    lib::memcpy(buffer, internal->data + offset, size);
    return size;
}

size_t ramfs::write(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept {
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0; // RAMFS is read-only for now via this interface
}

vfs_node* ramfs::readdir(vfs_node* node, size_t index) noexcept {
    ramfs_node_internal* internal = reinterpret_cast<ramfs_node_internal*>(node->ptr);
    ramfs_node_internal* cur = internal->first_child;
    size_t i = 0;
    while (cur && i < index) {
        cur = cur->next;
        i++;
    }
    return cur ? cur->vnode : nullptr;
}

vfs_node* ramfs::finddir(vfs_node* node, const char* name) noexcept {
    ramfs_node_internal* internal = reinterpret_cast<ramfs_node_internal*>(node->ptr);
    uint32_t hash = vfs_node::hash_name(name);
    ramfs_node_internal* cur = internal->first_child;
    while (cur) {
        if (cur->vnode->name_hash == hash && lib::strcmp(cur->vnode->name, name) == 0)
            return cur->vnode;
        cur = cur->next;
    }
    return nullptr;
}

} // namespace kernel::vfs
