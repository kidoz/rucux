// SPDX-License-Identifier: MIT
#include <kernel/print.hpp>
#include <kernel/sync/atomic.hpp>
#include <kernel/vfs/ramfs.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::vfs {

struct ramfs_node_internal {
    vfs_node* vnode;
    uint8_t* data;
    size_t size;
    ramfs_node_internal* next;        // Sibling
    ramfs_node_internal* first_child; // For directories
    atomic<bool> busy{false};
};

// Reserve without spinning or sleeping across allocation or file data access.
struct ramfs_operation_guard {
    ramfs_node_internal* internal;
    bool acquired;
    explicit ramfs_operation_guard(vfs_node* node) noexcept
        : internal(reinterpret_cast<ramfs_node_internal*>(node->ptr)) {
        bool expected = false;
        acquired = internal->busy.compare_exchange_strong(expected, true, acquire, relaxed);
    }
    ~ramfs_operation_guard() noexcept {
        if (acquired) internal->busy.store(false, release);
    }
    ramfs_operation_guard(const ramfs_operation_guard&) = delete;
    ramfs_operation_guard& operator=(const ramfs_operation_guard&) = delete;
};

static vfs_ops g_ramfs_ops = {.read = ramfs::read,
                              .write = ramfs::write,
                              .truncate = ramfs::truncate,
                              .open = nullptr,
                              .close = nullptr,
                              .ioctl = nullptr,
                              .readdir = ramfs::readdir,
                              .lookup = ramfs::lookup,
                              .mmap = nullptr,
                              .fsync = ramfs::fsync,
                              .poll = nullptr,
                              .append = ramfs::append};

static char* kstrdup(const char* s) noexcept {
    size_t len = 0;
    while (s[len])
        len++;
    char* dup = new char[len + 1];
    if (!dup) return nullptr;
    for (size_t i = 0; i <= len; ++i)
        dup[i] = s[i];
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

long ramfs::read(vfs_node* node, size_t offset, size_t size, void* buffer) noexcept {
    if (!node || !node->ptr || node->type != file_type::REGULAR) return -22;
    if (!size) return 0;
    if (!buffer) return -14;
    ramfs_operation_guard guard(node);
    if (!guard.acquired) return -16;
    ramfs_node_internal* internal = reinterpret_cast<ramfs_node_internal*>(node->ptr);
    if (offset >= node->length) return 0;
    if (!internal->data || node->length > internal->size) return -5;
    if (size > node->length - offset) size = node->length - offset;
    if (size > static_cast<size_t>(__LONG_MAX__)) size = static_cast<size_t>(__LONG_MAX__);
    lib::memcpy(buffer, internal->data + offset, size);
    return static_cast<long>(size);
}

// Caller owns the node reservation, including allocation and EOF publication.
static size_t write_data(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept {
    if (!node || (!buffer && size != 0) || size > SIZE_MAX - offset) return 0;
    if (size == 0) return 0;

    auto* internal = reinterpret_cast<ramfs_node_internal*>(node->ptr);
    size_t required = offset + size;
    if (required > internal->size) {
        auto* expanded = new uint8_t[required];
        if (!expanded) return 0;
        lib::memset(expanded, 0, required);
        if (internal->data && internal->size != 0) {
            lib::memcpy(expanded, internal->data, internal->size);
        }
        delete[] internal->data;
        internal->data = expanded;
        internal->size = required;
    }

    // Capacity may exceed EOF after a shrink. Never expose discarded bytes
    // when a later write creates a gap within that retained allocation.
    if (offset > node->length) lib::memset(internal->data + node->length, 0, offset - node->length);
    lib::memcpy(internal->data + offset, buffer, size);
    if (required > node->length) node->length = required;
    return size;
}

long ramfs::write(vfs_node* node, size_t offset, size_t size, const void* buffer) noexcept {
    if (!node || !node->ptr || node->type != file_type::REGULAR) return -22;
    if (!size) return 0;
    if (!buffer) return -14;
    if (offset > static_cast<size_t>(__LONG_MAX__) || size > static_cast<size_t>(__LONG_MAX__) - offset) return -75;
    ramfs_operation_guard guard(node);
    if (!guard.acquired) return -16;
    const auto done = write_data(node, offset, size, buffer);
    return done ? static_cast<long>(done) : -12;
}

long ramfs::append(vfs_node* node, size_t size, const void* buffer, size_t& end_offset) noexcept {
    if (!node || node->type != file_type::REGULAR || !node->ptr) return -22;
    if (!size) return 0;
    if (!buffer) return -14;
    ramfs_operation_guard guard(node);
    if (!guard.acquired) return -16;
    if (node->length > static_cast<size_t>(__LONG_MAX__) || size > static_cast<size_t>(__LONG_MAX__) - node->length)
        return -75;
    const auto done = write_data(node, node->length, size, buffer);
    if (!done) return -12;
    end_offset = node->length;
    return static_cast<long>(done);
}

int ramfs::truncate(vfs_node* node, size_t length) noexcept {
    if (!node || node->type != file_type::REGULAR || !node->ptr) return -22;
    ramfs_operation_guard guard(node);
    if (!guard.acquired) return -16;
    if (length > node->length) return -95;
    node->length = length;
    return 0;
}

int ramfs::fsync(vfs_node* node) noexcept {
    // RAMFS has no backing device or delayed writeback; it remains volatile.
    if (!node || !node->ptr || (node->type != file_type::REGULAR && node->type != file_type::DIRECTORY)) return -22;
    ramfs_operation_guard guard(node);
    return guard.acquired ? 0 : -16;
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

int ramfs::lookup(vfs_node* node, const char* name, vfs_node*& result) noexcept {
    result = nullptr;
    if (!node || !name || !*name || !node->ptr) return -22;
    if (node->type != file_type::DIRECTORY) return -20;
    auto* internal = reinterpret_cast<ramfs_node_internal*>(node->ptr);
    const uint32_t hash = vfs_node::hash_name(name);
    auto* cur = internal->first_child;
    while (cur) {
        if (cur->vnode->name_hash == hash && lib::strcmp(cur->vnode->name, name) == 0) {
            result = cur->vnode;
            return 0;
        }
        cur = cur->next;
    }
    return -2;
}

} // namespace kernel::vfs
