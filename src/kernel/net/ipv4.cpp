// SPDX-License-Identifier: MIT
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/print.hpp>

// Forward declarations for transport layer dispatch
namespace kernel::net {
void tcp_input(netif* iface, netbuf* buf) noexcept;
void udp_input(netif* iface, netbuf* buf) noexcept;
}

namespace kernel::net {

static uint16_t g_ip_id = 0;

void ipv4_input(netif* iface, netbuf* buf) noexcept {
    if (buf->len() < IPV4_HEADER_LEN) {
        netbuf::free(buf);
        return;
    }

    auto* ip = reinterpret_cast<ipv4_header*>(buf->data());

    // Validate version
    if ((ip->ihl_version >> 4) != 4) {
        netbuf::free(buf);
        return;
    }

    // Validate header checksum
    size_t hdr_len = (ip->ihl_version & 0x0F) * 4;
    if (checksum(ip, hdr_len) != 0) {
        netbuf::free(buf);
        return;
    }

    // Check destination: must be for us, broadcast, or loopback
    if (ip->dst_addr != iface->ip.addr &&
        ip->dst_addr != 0xFFFFFFFF &&
        (ip->dst_addr | iface->ip.netmask) != 0xFFFFFFFF &&
        iface != netif_loopback()) {
        netbuf::free(buf);
        return;
    }

    // Store metadata for transport layer
    buf->src_ip = ip->src_addr;
    buf->dst_ip = ip->dst_addr;
    buf->protocol = ip->protocol;

    // Strip IP header
    buf->pull(hdr_len);

    switch (ip->protocol) {
    case IPPROTO_TCP: tcp_input(iface, buf); break;
    case IPPROTO_UDP: udp_input(iface, buf); break;
    default:          netbuf::free(buf); break;
    }
}

void ipv4_output(netbuf* buf, uint32_t src, uint32_t dst, uint8_t protocol) noexcept {
    // Prepend IPv4 header
    auto* ip = reinterpret_cast<ipv4_header*>(buf->push(IPV4_HEADER_LEN));
    ip->ihl_version = 0x45; // Version 4, IHL 5 (20 bytes)
    ip->tos = 0;
    ip->total_length = htons(static_cast<uint16_t>(buf->len()));
    ip->identification = htons(g_ip_id++);
    ip->flags_fragment = htons(0x4000); // Don't Fragment
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_addr = src;
    ip->dst_addr = dst;
    ip->checksum = checksum(ip, IPV4_HEADER_LEN);

    // Route: find the right interface
    netif* iface = nullptr;
    if (dst == 0x0100007F) { // 127.0.0.1
        iface = netif_loopback();
    } else {
        iface = netif_default();
    }

    if (!iface) {
        netbuf::free(buf);
        return;
    }

    if (iface == netif_loopback()) {
        // Loopback: deliver directly (no Ethernet framing)
        iface->transmit(iface, buf);
    } else {
        // Physical: add Ethernet framing
        ethernet_output(iface, buf, dst, ETH_TYPE_IPV4);
    }
}

} // namespace kernel::net
