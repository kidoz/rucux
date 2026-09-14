// SPDX-License-Identifier: MIT
#include <kernel/net/arp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/sync/spinlock.hpp>
#include <kernel/time.hpp>
#include <lib/string.hpp>

namespace kernel::net {
namespace {
constexpr size_t CACHE_SIZE = 64, PENDING_NEIGHBORS = 8, PACKETS_PER_NEIGHBOR = 4;
struct Entry {
    netif* iface;
    uint32_t ip;
    uint8_t mac[6];
    uint64_t expires;
};
struct Pending {
    netif* iface;
    uint32_t ip;
    netbuf* head;
    netbuf* tail;
    unsigned count;
    unsigned attempts;
    uint64_t retry_at;
    uint64_t expires;
};
Entry cache[CACHE_SIZE]{};
Pending pending[PENDING_NEIGHBORS]{};
irq_spinlock lock;

Entry* find(netif* iface, uint32_t ip) noexcept {
    auto now = time_manager::get_ticks();
    for (auto& entry : cache)
        if (entry.iface == iface && entry.ip == ip && (!entry.expires || now < entry.expires)) return &entry;
    return nullptr;
}

void transmit(netif* iface, netbuf* buf, const uint8_t mac[6]) noexcept {
    // Pending packets already carry their Ethernet header, but no destination.
    auto* eth = reinterpret_cast<eth_header*>(buf->data());
    lib::memcpy(eth->dst, mac, 6);
    iface->transmit(iface, buf);
}

void request(netif* iface, uint16_t op, uint32_t target, const uint8_t mac[6]) noexcept {
    auto* buf = netbuf::alloc();
    if (!buf) return;
    auto* arp = reinterpret_cast<arp_header*>(buf->put(sizeof(arp_header)));
    *arp = {};
    arp->hw_type = htons(1);
    arp->proto_type = htons(ETH_TYPE_IPV4);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->operation = htons(op);
    lib::memcpy(arp->sender_mac, iface->mac.bytes, 6);
    arp->sender_ip = iface->ip.addr;
    lib::memcpy(arp->target_mac, mac, 6);
    arp->target_ip = target;
    auto* eth = reinterpret_cast<eth_header*>(buf->push(ETH_HEADER_LEN));
    if (op == ARP_OP_REQUEST)
        lib::memset(eth->dst, 0xFF, 6);
    else
        lib::memcpy(eth->dst, mac, 6);
    lib::memcpy(eth->src, iface->mac.bytes, 6);
    eth->ethertype = htons(ETH_TYPE_ARP);
    iface->transmit(iface, buf);
}

void learn(netif* iface, uint32_t ip, const uint8_t mac[6], bool permanent) noexcept {
    netbuf* ready = nullptr;
    {
        irq_lock_guard guard(lock);
        auto* entry = find(iface, ip);
        if (!entry) {
            auto now = time_manager::get_ticks();
            for (auto& candidate : cache)
                if (!candidate.iface || (candidate.expires && now >= candidate.expires)) {
                    entry = &candidate;
                    break;
                }
        }
        if (!entry) entry = &cache[0];
        entry->iface = iface;
        entry->ip = ip;
        lib::memcpy(entry->mac, mac, 6);
        entry->expires = permanent ? 0 : time_manager::get_ticks() + 60000;
        for (auto& p : pending)
            if (p.iface == iface && p.ip == ip) {
                ready = p.head;
                p = {};
                break;
            }
    }
    // Publish the cache and detach the queue before calling any driver/peer.
    while (ready) {
        auto* next = ready->next;
        ready->next = nullptr;
        transmit(iface, ready, mac);
        ready = next;
    }
}
} // namespace

void arp_add_static(netif* iface, uint32_t ip, const uint8_t mac[6]) noexcept {
    learn(iface, ip, mac, true);
}

void arp_output(netif* iface, netbuf* buf, uint32_t next_hop) noexcept {
    uint8_t mac[6]{};
    bool resolved = false, ask = false;
    {
        irq_lock_guard guard(lock);
        if (next_hop == 0xFFFFFFFF || next_hop == (iface->ip.addr | ~iface->ip.netmask)) {
            lib::memset(mac, 0xFF, 6);
            resolved = true;
        } else if (auto* entry = find(iface, next_hop)) {
            lib::memcpy(mac, entry->mac, 6);
            resolved = true;
        } else {
            Pending* slot = nullptr;
            for (auto& p : pending)
                if (p.iface == iface && p.ip == next_hop) {
                    slot = &p;
                    break;
                }
            if (!slot)
                for (auto& p : pending)
                    if (!p.iface) {
                        slot = &p;
                        p.iface = iface;
                        p.ip = next_hop;
                        p.attempts = 1;
                        p.retry_at = time_manager::get_ticks() + 1000;
                        p.expires = p.retry_at + 2000;
                        ask = true;
                        break;
                    }
            if (!slot || slot->count == PACKETS_PER_NEIGHBOR) {
                netbuf::free(buf);
                return;
            }
            buf->next = nullptr;
            if (slot->tail)
                slot->tail->next = buf;
            else
                slot->head = buf;
            slot->tail = buf;
            ++slot->count;
        }
    }
    if (resolved)
        transmit(iface, buf, mac);
    else if (ask)
        request(iface, ARP_OP_REQUEST, next_hop, mac);
}

void arp_input(netif* iface, netbuf* buf) noexcept {
    if (buf->len() < sizeof(arp_header)) {
        netbuf::free(buf);
        return;
    }
    auto* arp = reinterpret_cast<arp_header*>(buf->data());
    uint16_t op = ntohs(arp->operation);
    if (ntohs(arp->hw_type) != 1 || ntohs(arp->proto_type) != ETH_TYPE_IPV4 || arp->hw_len != 6 ||
        arp->proto_len != 4 || (op != ARP_OP_REQUEST && op != ARP_OP_REPLY) || arp->target_ip != iface->ip.addr ||
        !arp->sender_ip || (arp->sender_mac[0] & 1) ||
        (op == ARP_OP_REPLY && lib::memcmp(arp->target_mac, iface->mac.bytes, 6))) {
        netbuf::free(buf);
        return;
    }
    learn(iface, arp->sender_ip, arp->sender_mac, false);
    if (op == ARP_OP_REQUEST) request(iface, ARP_OP_REPLY, arp->sender_ip, arp->sender_mac);
    netbuf::free(buf);
}

void arp_tick(uint64_t now) noexcept {
    for (size_t i = 0; i < PENDING_NEIGHBORS; ++i) {
        netif* iface = nullptr;
        uint32_t ip = 0;
        {
            irq_lock_guard guard(lock);
            auto& p = pending[i];
            if (!p.iface || now < p.retry_at) continue;
            if (now >= p.expires || p.attempts == 3) {
                while (p.head) {
                    auto* next = p.head->next;
                    netbuf::free(p.head);
                    p.head = next;
                }
                p = {};
            } else {
                iface = p.iface;
                ip = p.ip;
                ++p.attempts;
                p.retry_at = now + 1000;
            }
        }
        if (iface) {
            uint8_t zero[6]{};
            request(iface, ARP_OP_REQUEST, ip, zero);
        }
    }
}
} // namespace kernel::net
