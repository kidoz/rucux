// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>
#include <kernel/net/udp.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::net {

static udp_pcb* g_udp_list = nullptr;
static irq_spinlock g_udp_lock;
[[maybe_unused]] static uint16_t g_udp_ephemeral = 49152;

void udp_init() noexcept {
    g_udp_list = nullptr;
}

static udp_pcb* find_udp_pcb(uint16_t local_port) noexcept {
    for (auto* p = g_udp_list; p; p = p->next)
        if (p->local_port == local_port) return p;
    return nullptr;
}

void udp_input(netif* iface, netbuf* buf) noexcept {
    (void)iface;
    if (buf->len() < UDP_HEADER_LEN) { netbuf::free(buf); return; }

    auto* udp = reinterpret_cast<udp_header*>(buf->data());

    irq_lock_guard guard(g_udp_lock);
    auto* pcb = find_udp_pcb(udp->dst_port);
    if (!pcb) { netbuf::free(buf); return; }

    // Strip UDP header but keep src info
    buf->src_port = udp->src_port;
    buf->dst_port = udp->dst_port;
    buf->pull(UDP_HEADER_LEN);

    // Enqueue to PCB receive list
    uintptr_t flags = pcb->lock.lock();
    buf->next = nullptr;
    if (!pcb->recv_head) { pcb->recv_head = pcb->recv_tail = buf; }
    else { pcb->recv_tail->next = buf; pcb->recv_tail = buf; }
    pcb->recv_count++;

    if (pcb->wait_recv) {
        scheduler::scheduler::unblock(pcb->wait_recv);
        pcb->wait_recv = nullptr;
    }
    pcb->lock.unlock(flags);
}

udp_pcb* udp_new() noexcept {
    auto* pcb = new udp_pcb();
    if (!pcb) return nullptr;
    lib::memset(pcb, 0, sizeof(udp_pcb));

    irq_lock_guard guard(g_udp_lock);
    pcb->next = g_udp_list;
    g_udp_list = pcb;
    return pcb;
}

void udp_free(udp_pcb* pcb) noexcept {
    if (!pcb) return;
    irq_lock_guard guard(g_udp_lock);
    udp_pcb** pp = &g_udp_list;
    while (*pp) {
        if (*pp == pcb) { *pp = pcb->next; break; }
        pp = &(*pp)->next;
    }
    // Free any queued packets
    while (pcb->recv_head) {
        auto* nb = pcb->recv_head;
        pcb->recv_head = nb->next;
        netbuf::free(nb);
    }
    delete pcb;
}

int udp_bind(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    pcb->local_ip = addr;
    pcb->local_port = port;
    pcb->bound = true;
    return 0;
}

int udp_connect(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    pcb->remote_ip = addr;
    pcb->remote_port = port;
    pcb->connected = true;
    return 0;
}

long udp_sendto(udp_pcb* pcb, const void* data, size_t len,
                uint32_t dst_ip, uint16_t dst_port) noexcept {
    auto* buf = netbuf::alloc();
    if (!buf) return -1;

    // Payload
    auto* payload = buf->put(len);
    lib::memcpy(payload, data, len);

    // UDP header
    auto* udp = reinterpret_cast<udp_header*>(buf->push(UDP_HEADER_LEN));
    udp->src_port = pcb->local_port;
    udp->dst_port = dst_port;
    udp->length = htons(static_cast<uint16_t>(UDP_HEADER_LEN + len));
    udp->checksum = 0; // Optional for UDP over IPv4

    uint32_t src = pcb->local_ip;
    if (src == 0) {
        auto* iface = netif_default();
        if (iface) src = iface->ip.addr;
    }

    ipv4_output(buf, src, dst_ip, IPPROTO_UDP);
    return static_cast<long>(len);
}

long udp_recvfrom(udp_pcb* pcb, void* data, size_t len,
                  uint32_t* src_ip, uint16_t* src_port) noexcept {
    // Block until data
    while (true) {
        uintptr_t flags = pcb->lock.lock();
        if (pcb->recv_head) {
            auto* nb = pcb->recv_head;
            pcb->recv_head = nb->next;
            if (!pcb->recv_head) pcb->recv_tail = nullptr;
            pcb->recv_count--;
            pcb->lock.unlock(flags);

            size_t to_copy = (len < nb->len()) ? len : nb->len();
            lib::memcpy(data, nb->data(), to_copy);
            if (src_ip) *src_ip = nb->src_ip;
            if (src_port) *src_port = nb->src_port;
            netbuf::free(nb);
            return static_cast<long>(to_copy);
        }
        pcb->wait_recv = scheduler::scheduler::current_thread();
        pcb->lock.unlock(flags);
        scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
    }
}

int udp_poll_events(udp_pcb* pcb) noexcept {
    int events = 4; // POLLOUT always
    if (pcb->recv_head) events |= 1; // POLLIN
    return events;
}

} // namespace kernel::net
