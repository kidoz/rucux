// SPDX-License-Identifier: MIT
#include <kernel/net/arp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/print.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/string.hpp>

namespace kernel::net {

// ─── ARP cache ─────────────────────────────────────────────────────────────

struct arp_entry {
    uint32_t ip;
    uint8_t mac[6];
    bool valid;
};

static constexpr size_t ARP_CACHE_SIZE = 64;
static arp_entry g_arp_cache[ARP_CACHE_SIZE] = {};
static irq_spinlock g_arp_lock;

static arp_entry* arp_find(uint32_t ip) noexcept {
    for (auto& e : g_arp_cache)
        if (e.valid && e.ip == ip) return &e;
    return nullptr;
}

static void arp_update(uint32_t ip, const uint8_t mac[6]) noexcept {
    // Update existing or add new
    auto* e = arp_find(ip);
    if (e) {
        lib::memcpy(e->mac, mac, 6);
        return;
    }
    // Find empty slot
    for (auto& entry : g_arp_cache) {
        if (!entry.valid) {
            entry.ip = ip;
            lib::memcpy(entry.mac, mac, 6);
            entry.valid = true;
            return;
        }
    }
    // Cache full — overwrite first entry (simple eviction)
    g_arp_cache[0].ip = ip;
    lib::memcpy(g_arp_cache[0].mac, mac, 6);
}

void arp_add_static(uint32_t ip, const uint8_t mac[6]) noexcept {
    irq_lock_guard guard(g_arp_lock);
    arp_update(ip, mac);
}

// ─── ARP send ──────────────────────────────────────────────────────────────

static void arp_send(netif* iface, uint16_t op, uint32_t target_ip, const uint8_t target_mac[6]) noexcept {
    auto* buf = netbuf::alloc();
    if (!buf) return;

    auto* arp = reinterpret_cast<arp_header*>(buf->put(sizeof(arp_header)));
    arp->hw_type = htons(1);         // Ethernet
    arp->proto_type = htons(0x0800); // IPv4
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->operation = htons(op);
    lib::memcpy(arp->sender_mac, iface->mac.bytes, 6);
    arp->sender_ip = iface->ip.addr;
    lib::memcpy(arp->target_mac, target_mac, 6);
    arp->target_ip = target_ip;

    // Prepend Ethernet header
    auto* eth = reinterpret_cast<eth_header*>(buf->push(ETH_HEADER_LEN));
    if (op == ARP_OP_REQUEST) {
        lib::memset(eth->dst, 0xFF, 6); // Broadcast
    } else {
        lib::memcpy(eth->dst, target_mac, 6);
    }
    lib::memcpy(eth->src, iface->mac.bytes, 6);
    eth->ethertype = htons(ETH_TYPE_ARP);

    iface->transmit(iface, buf);
}

// ─── ARP input ─────────────────────────────────────────────────────────────

void arp_input(netif* iface, netbuf* buf) noexcept {
    if (buf->len() < sizeof(arp_header)) {
        netbuf::free(buf);
        return;
    }

    auto* arp = reinterpret_cast<arp_header*>(buf->data());
    uint16_t op = ntohs(arp->operation);

    // Update cache with sender's info
    {
        irq_lock_guard guard(g_arp_lock);
        arp_update(arp->sender_ip, arp->sender_mac);
    }

    if (op == ARP_OP_REQUEST && arp->target_ip == iface->ip.addr) {
        // It's asking for us — send a reply
        arp_send(iface, ARP_OP_REPLY, arp->sender_ip, arp->sender_mac);
    }

    netbuf::free(buf);
}

// ─── ARP resolve ───────────────────────────────────────────────────────────

bool arp_resolve(netif* iface, uint32_t ip, uint8_t out_mac[6]) noexcept {
    // Broadcast?
    if (ip == 0xFFFFFFFF || (ip | iface->ip.netmask) == 0xFFFFFFFF) {
        lib::memset(out_mac, 0xFF, 6);
        return true;
    }

    irq_lock_guard guard(g_arp_lock);
    auto* e = arp_find(ip);
    if (e) {
        lib::memcpy(out_mac, e->mac, 6);
        return true;
    }

    // Not in cache — send ARP request
    uint8_t zero_mac[6] = {};
    arp_send(iface, ARP_OP_REQUEST, ip, zero_mac);
    return false;
}

} // namespace kernel::net
