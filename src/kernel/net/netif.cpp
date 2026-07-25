// SPDX-License-Identifier: MIT
#include <kernel/net/netif.hpp>
#include <kernel/print.hpp>
#include <lib/string.hpp>

// Forward declarations for protocol dispatch
namespace kernel::net {
void ethernet_input(netif* iface, netbuf* buf) noexcept;
}

namespace kernel::net {

// ─── Global interface list ─────────────────────────────────────────────────

static netif* g_iface_list = nullptr;
static netif* g_loopback = nullptr;

void netif::enqueue_tx(netbuf* buf) noexcept {
    uintptr_t flags = tx_lock.lock();
    buf->next = nullptr;
    if (!tx_head) {
        tx_head = tx_tail = buf;
    } else {
        tx_tail->next = buf;
        tx_tail = buf;
    }
    tx_lock.unlock(flags);
}

netbuf* netif::dequeue_tx() noexcept {
    uintptr_t flags = tx_lock.lock();
    netbuf* buf = tx_head;
    if (buf) {
        tx_head = buf->next;
        if (!tx_head) tx_tail = nullptr;
        buf->next = nullptr;
    }
    tx_lock.unlock(flags);
    return buf;
}

void netif_register(netif* iface) noexcept {
    iface->next_iface = g_iface_list;
    iface->tx_head = iface->tx_tail = nullptr;
    g_iface_list = iface;
    kernel::print("NET: registered interface '{}' IP={}.{}.{}.{}\n", iface->name, (iface->ip.addr >> 0) & 0xFF,
                  (iface->ip.addr >> 8) & 0xFF, (iface->ip.addr >> 16) & 0xFF, (iface->ip.addr >> 24) & 0xFF);
}

netif* netif_default() noexcept {
    // Return first interface with a gateway configured, else first non-loopback
    for (auto* i = g_iface_list; i; i = i->next_iface)
        if (i->ip.gateway != 0) return i;
    for (auto* i = g_iface_list; i; i = i->next_iface)
        if (i != g_loopback) return i;
    return g_loopback;
}

netif* netif_find(const char* name) noexcept {
    for (auto* i = g_iface_list; i; i = i->next_iface)
        if (lib::strcmp(i->name, name) == 0) return i;
    return nullptr;
}

netif* netif_loopback() noexcept {
    return g_loopback;
}

// ─── Loopback interface ────────────────────────────────────────────────────

static void loopback_tx(netif* iface, netbuf* buf) noexcept {
    // Loopback: deliver the packet back to the input path directly
    // Skip Ethernet framing — loopback packets start at IP header
    netif_input(iface, buf);
}

static netif g_lo_iface;

// ─── Packet input dispatch ─────────────────────────────────────────────────

void netif_input(netif* iface, netbuf* buf) noexcept {
    if (iface == g_loopback) {
        // Loopback: buf starts at IP header, dispatch directly
        extern void ipv4_input(netif*, netbuf*) noexcept;
        ipv4_input(iface, buf);
    } else {
        // Physical interface: buf starts at Ethernet header
        ethernet_input(iface, buf);
    }
}

// ─── Init ──────────────────────────────────────────────────────────────────

void net_init() noexcept {
    // Create loopback interface
    g_lo_iface.name = "lo";
    lib::memset(g_lo_iface.mac.bytes, 0, 6);
    g_lo_iface.ip.addr = 0x0100007F;    // 127.0.0.1 in network byte order
    g_lo_iface.ip.netmask = 0x000000FF; // 255.0.0.0
    g_lo_iface.ip.gateway = 0;
    g_lo_iface.mtu = 65535;
    g_lo_iface.transmit = loopback_tx;
    g_loopback = &g_lo_iface;
    netif_register(&g_lo_iface);

    kernel::print("NET: TCP/IP stack initialized\n");
}

} // namespace kernel::net
