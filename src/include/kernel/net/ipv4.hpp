// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/net/netbuf.hpp>
#include <kernel/net/netif.hpp>
#include <stdint.h>

namespace kernel::net {

struct ipv4_header {
    uint8_t ihl_version; // version (4 bits) + IHL (4 bits)
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol; // 6=TCP, 17=UDP
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} __attribute__((packed));

static constexpr uint8_t IPPROTO_TCP = 6;
static constexpr uint8_t IPPROTO_UDP = 17;
static constexpr size_t IPV4_HEADER_LEN = 20;

// Process a received IPv4 packet (dispatches to TCP or UDP)
void ipv4_input(netif* iface, netbuf* buf) noexcept;

// Send an IPv4 packet: prepends IP header, routes to appropriate interface
void ipv4_output(netbuf* buf, uint32_t src, uint32_t dst, uint8_t protocol) noexcept;

// Compute Internet checksum (RFC 1071)
uint16_t checksum(const void* data, size_t len) noexcept;

// Compute TCP/UDP pseudo-header checksum
uint16_t checksum_pseudo(uint32_t src, uint32_t dst, uint8_t proto, const void* data, size_t len) noexcept;

// Byte order helpers (inline for performance)
inline uint16_t htons(uint16_t v) noexcept {
    return __builtin_bswap16(v);
}
inline uint32_t htonl(uint32_t v) noexcept {
    return __builtin_bswap32(v);
}
inline uint16_t ntohs(uint16_t v) noexcept {
    return __builtin_bswap16(v);
}
inline uint32_t ntohl(uint32_t v) noexcept {
    return __builtin_bswap32(v);
}

} // namespace kernel::net
