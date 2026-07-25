// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/net/netbuf.hpp>
#include <kernel/net/netif.hpp>
#include <stdint.h>

namespace kernel::net {

static constexpr uint16_t ARP_OP_REQUEST = 1;
static constexpr uint16_t ARP_OP_REPLY = 2;

struct arp_header {
    uint16_t hw_type;    // 1 = Ethernet
    uint16_t proto_type; // 0x0800 = IPv4
    uint8_t hw_len;      // 6 for Ethernet
    uint8_t proto_len;   // 4 for IPv4
    uint16_t operation;  // REQUEST=1, REPLY=2
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

// Process a received ARP packet
void arp_input(netif* iface, netbuf* buf) noexcept;

// Resolve an IPv4 address to a MAC address.
// Returns true and fills `out_mac` if cached.
// Returns false and sends an ARP request if not cached.
bool arp_resolve(netif* iface, uint32_t ip, uint8_t out_mac[6]) noexcept;

// Add a static ARP entry
void arp_add_static(uint32_t ip, const uint8_t mac[6]) noexcept;

} // namespace kernel::net
