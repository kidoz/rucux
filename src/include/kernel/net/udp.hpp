// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/net/ipv4.hpp>
#include <kernel/net/netbuf.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::net {

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

static constexpr size_t UDP_HEADER_LEN = 8;

// UDP Protocol Control Block
struct udp_pcb {
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    bool bound;
    bool connected;

    // Receive queue (chain of netbufs)
    netbuf* recv_head;
    netbuf* recv_tail;
    int recv_count;

    scheduler::thread* wait_recv;
    irq_spinlock lock;

    udp_pcb* next;
};

void udp_init() noexcept;
void udp_input(netif* iface, netbuf* buf) noexcept;

udp_pcb* udp_new() noexcept;
void udp_free(udp_pcb* pcb) noexcept;
int udp_bind(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept;
int udp_connect(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept;
long udp_sendto(udp_pcb* pcb, const void* data, size_t len,
                uint32_t dst_ip, uint16_t dst_port) noexcept;
long udp_recvfrom(udp_pcb* pcb, void* data, size_t len,
                  uint32_t* src_ip, uint16_t* src_port) noexcept;
int udp_poll_events(udp_pcb* pcb) noexcept;

} // namespace kernel::net
