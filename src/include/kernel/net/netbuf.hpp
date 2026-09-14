// SPDX-License-Identifier: MIT
#pragma once
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::net {

// Network buffer — holds a single packet with headroom for header prepending.
// Layout: [headroom | data ... | tailroom]
// push() prepends headers by moving data pointer backward (zero-copy).
// This is the fundamental packet container passed through every layer.

class netbuf {
public:
    static constexpr size_t DEFAULT_SIZE = 2048; // Enough for Ethernet MTU + headers
    static constexpr size_t HEADROOM = 128;      // Space for all headers (eth+ip+tcp)

    // Allocate a new netbuf with default size
    static netbuf* alloc() noexcept;
    static netbuf* alloc(size_t total_size) noexcept;
    static void free(netbuf* buf) noexcept;

    // Data pointer and length
    uint8_t* data() noexcept { return data_; }
    const uint8_t* data() const noexcept { return data_; }
    size_t len() const noexcept { return len_; }

    // Prepend header: moves data pointer back by `size` bytes, returns new data ptr.
    // Used when building a packet from app data → TCP → IP → Ethernet.
    uint8_t* push(size_t size) noexcept;

    // Consume header: moves data pointer forward by `size` bytes.
    // Used when parsing a received packet: Ethernet → IP → TCP → app data.
    uint8_t* pull(size_t size) noexcept;

    // Append data to the tail. Returns pointer to appended area.
    uint8_t* put(size_t size) noexcept;

    // Set data length explicitly (for received packets from NIC)
    bool set_len(size_t len) noexcept {
        if (len > total_size_ - static_cast<size_t>(data_ - buf_)) return false;
        len_ = len;
        return true;
    }

    // Total buffer capacity (for NIC DMA setup)
    size_t capacity() const noexcept { return total_size_ - HEADROOM; }
    uint8_t* buf_start() noexcept { return buf_; }
    size_t total_size() const noexcept { return total_size_; }

    // Linked list for queuing (TX queue, socket recv queue, etc.)
    netbuf* next;

    // Metadata set by lower layers
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol; // IPPROTO_TCP / IPPROTO_UDP

private:
    uint8_t* buf_;      // Start of allocated memory
    uint8_t* data_;     // Current data start
    size_t len_;        // Current data length
    size_t total_size_; // Total allocation
};

} // namespace kernel::net
