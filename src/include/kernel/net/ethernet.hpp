// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/net/netbuf.hpp>
#include <kernel/net/netif.hpp>
#include <stdint.h>

namespace kernel::net {

static constexpr uint16_t ETH_TYPE_IPV4 = 0x0800;
static constexpr uint16_t ETH_TYPE_ARP  = 0x0806;
static constexpr size_t   ETH_HEADER_LEN = 14;
static constexpr size_t   ETH_ADDR_LEN = 6;

struct eth_header {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype; // Network byte order
} __attribute__((packed));

// Process a received Ethernet frame (dispatches to ARP or IPv4)
void ethernet_input(netif* iface, netbuf* buf) noexcept;

// Send an IP packet: prepends Ethernet header, resolves MAC via ARP
void ethernet_output(netif* iface, netbuf* buf, uint32_t dst_ip, uint16_t ethertype) noexcept;

} // namespace kernel::net
