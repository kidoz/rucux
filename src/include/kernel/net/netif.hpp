// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/net/netbuf.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>
#include <uapi/kernel/net.h>

namespace kernel::net {

// Network interface — abstraction over a NIC or virtual device (loopback).
// Drivers register a netif and provide tx/rx callbacks.

struct mac_addr {
    uint8_t bytes[6];
};

struct ipv4_config {
    uint32_t addr;    // Host IP (network byte order)
    uint32_t netmask; // e.g., 255.255.255.0
    uint32_t gateway; // Default gateway
};

class netif {
public:
    // Driver must set these before registering
    const char* name; // e.g., "eth0", "lo"
    mac_addr mac;
    ipv4_config ip;
    uint32_t mtu; // Maximum transmission unit (e.g., 1500)

    // Driver callback: transmit a packet. Takes ownership of buf.
    void (*transmit)(netif* iface, netbuf* buf) noexcept;

    // For linking in the global interface list
    netif* next_iface;

    // TX queue (packets waiting for NIC to be ready)
    netbuf* tx_head;
    netbuf* tx_tail;
    irq_spinlock tx_lock;

    void enqueue_tx(netbuf* buf) noexcept;
    netbuf* dequeue_tx() noexcept;
};

// ─── Global interface management ───────────────────────────────────────────

// Register a new network interface
void netif_register(netif* iface) noexcept;

// Get the default interface (first registered, or the one with a gateway)
netif* netif_default() noexcept;

// Find interface by name
netif* netif_find(const char* name) noexcept;

// Get the loopback interface
netif* netif_loopback() noexcept;

// Called by NIC driver when a packet is received. Dispatches up the stack.
void netif_input(netif* iface, netbuf* buf) noexcept;

// Initialize the network subsystem (creates loopback, etc.)
void net_init() noexcept;
int net_get_info(rucux_net_info* info) noexcept;

} // namespace kernel::net
