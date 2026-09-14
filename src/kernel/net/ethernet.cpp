// SPDX-License-Identifier: MIT
#include <kernel/net/arp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>
#include <lib/string.hpp>

namespace kernel::net {

void ethernet_input(netif* iface, netbuf* buf) noexcept {
    if (buf->len() < ETH_HEADER_LEN) {
        netbuf::free(buf);
        return;
    }

    auto* eth = reinterpret_cast<eth_header*>(buf->data());
    uint16_t type = ntohs(eth->ethertype);

    buf->pull(ETH_HEADER_LEN); // Strip Ethernet header

    switch (type) {
    case ETH_TYPE_IPV4:
        ipv4_input(iface, buf);
        break;
    case ETH_TYPE_ARP:
        arp_input(iface, buf);
        break;
    default:
        netbuf::free(buf);
        break;
    }
}

void ethernet_output(netif* iface, netbuf* buf, uint32_t dst_ip, uint16_t ethertype) noexcept {
    // Determine next-hop IP (use gateway if destination is off-subnet)
    uint32_t next_hop = dst_ip;
    if (dst_ip != 0xFFFFFFFF && (dst_ip & iface->ip.netmask) != (iface->ip.addr & iface->ip.netmask)) {
        next_hop = iface->ip.gateway;
        if (!next_hop) {
            netbuf::free(buf);
            return; // No route to host
        }
    }

    // Prepend Ethernet header
    auto* eth = reinterpret_cast<eth_header*>(buf->push(ETH_HEADER_LEN));
    if (!eth) {
        netbuf::free(buf);
        return;
    }
    lib::memset(eth->dst, 0, 6);
    lib::memcpy(eth->src, iface->mac.bytes, 6);
    eth->ethertype = htons(ethertype);

    arp_output(iface, buf, next_hop);
}

} // namespace kernel::net
