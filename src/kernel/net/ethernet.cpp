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
    case ETH_TYPE_IPV4: ipv4_input(iface, buf); break;
    case ETH_TYPE_ARP:  arp_input(iface, buf); break;
    default:            netbuf::free(buf); break;
    }
}

void ethernet_output(netif* iface, netbuf* buf, uint32_t dst_ip, uint16_t ethertype) noexcept {
    // Determine next-hop IP (use gateway if destination is off-subnet)
    uint32_t next_hop = dst_ip;
    if ((dst_ip & iface->ip.netmask) != (iface->ip.addr & iface->ip.netmask)) {
        next_hop = iface->ip.gateway;
        if (!next_hop) {
            netbuf::free(buf);
            return; // No route to host
        }
    }

    // Resolve MAC address via ARP
    uint8_t dst_mac[6];
    if (!arp_resolve(iface, next_hop, dst_mac)) {
        // ARP request sent, queue packet for later
        // TODO: implement ARP pending queue (for now, drop the packet)
        netbuf::free(buf);
        return;
    }

    // Prepend Ethernet header
    auto* eth = reinterpret_cast<eth_header*>(buf->push(ETH_HEADER_LEN));
    lib::memcpy(eth->dst, dst_mac, 6);
    lib::memcpy(eth->src, iface->mac.bytes, 6);
    eth->ethertype = htons(ethertype);

    iface->transmit(iface, buf);
}

} // namespace kernel::net
