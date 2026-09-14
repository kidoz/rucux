// SPDX-License-Identifier: MIT
#include <kernel/memory/heap.hpp>
#include <kernel/net/netbuf.hpp>
#include <lib/string.hpp>

namespace kernel::net {

netbuf* netbuf::alloc() noexcept {
    return alloc(DEFAULT_SIZE);
}

netbuf* netbuf::alloc(size_t total_size) noexcept {
    if (total_size < HEADROOM || total_size > SIZE_MAX - sizeof(netbuf)) return nullptr;
    // Allocate netbuf struct + buffer in a single allocation
    size_t alloc_size = sizeof(netbuf) + total_size;
    auto* raw = reinterpret_cast<uint8_t*>(kmalloc(alloc_size));
    if (!raw) return nullptr;

    auto* nb = reinterpret_cast<netbuf*>(raw);
    nb->buf_ = raw + sizeof(netbuf);
    nb->data_ = nb->buf_ + HEADROOM;
    nb->len_ = 0;
    nb->total_size_ = total_size;
    nb->next = nullptr;
    nb->src_ip = nb->dst_ip = 0;
    nb->src_port = nb->dst_port = 0;
    nb->protocol = 0;
    return nb;
}

void netbuf::free(netbuf* buf) noexcept {
    if (buf) kfree(buf);
}

uint8_t* netbuf::push(size_t size) noexcept {
    if (size > static_cast<size_t>(data_ - buf_)) return nullptr;
    data_ -= size;
    len_ += size;
    return data_;
}

uint8_t* netbuf::pull(size_t size) noexcept {
    if (size > len_) return nullptr;
    data_ += size;
    len_ -= size;
    return data_;
}

uint8_t* netbuf::put(size_t size) noexcept {
    if (size > total_size_ - static_cast<size_t>(data_ - buf_) - len_) return nullptr;
    uint8_t* tail = data_ + len_;
    len_ += size;
    return tail;
}

} // namespace kernel::net
