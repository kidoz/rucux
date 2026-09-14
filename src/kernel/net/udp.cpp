// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>
#include <kernel/net/udp.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::net {

static udp_pcb* g_udp_list = nullptr;
static irq_spinlock g_udp_lock;
static uint16_t g_udp_ephemeral = 49152;

void udp_init() noexcept {
    g_udp_list = nullptr;
}

static udp_pcb* find_udp_pcb(uint32_t addr, uint16_t port, uint32_t remote, uint16_t remote_port) noexcept {
    for (auto* p = g_udp_list; p; p = p->next)
        if (p->bound && p->local_port == port && (!p->local_ip || p->local_ip == addr) &&
            (!p->connected || (p->remote_ip == remote && p->remote_port == remote_port)))
            return p;
    return nullptr;
}

void udp_input(netif* iface, netbuf* buf) noexcept {
    (void)iface;
    if (buf->len() < UDP_HEADER_LEN) {
        netbuf::free(buf);
        return;
    }

    auto* udp = reinterpret_cast<udp_header*>(buf->data());

    const size_t length = ntohs(udp->length);
    if (length < UDP_HEADER_LEN || length > buf->len() ||
        (udp->checksum && checksum_pseudo(buf->src_ip, buf->dst_ip, IPPROTO_UDP, udp, length))) {
        netbuf::free(buf);
        return;
    }
    buf->set_len(length);
    irq_lock_guard guard(g_udp_lock);
    auto* pcb = find_udp_pcb(buf->dst_ip, udp->dst_port, buf->src_ip, udp->src_port);
    if (!pcb || pcb->recv_count >= 32) {
        netbuf::free(buf);
        return;
    }

    // Strip UDP header but keep src info
    buf->src_port = udp->src_port;
    buf->dst_port = udp->dst_port;
    buf->pull(UDP_HEADER_LEN);

    // Enqueue to PCB receive list
    // The global UDP lock protects lookup, queues and wait enrollment.
    buf->next = nullptr;
    if (!pcb->recv_head) {
        pcb->recv_head = pcb->recv_tail = buf;
    } else {
        pcb->recv_tail->next = buf;
        pcb->recv_tail = buf;
    }
    pcb->recv_count++;

    if (pcb->wait_recv) {
        scheduler::scheduler::unblock(pcb->wait_recv);
        pcb->wait_recv = nullptr;
    }
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
        if (*pp == pcb) {
            *pp = pcb->next;
            break;
        }
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

static bool port_used(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    for (auto* p = g_udp_list; p; p = p->next)
        if (p != pcb && p->bound && p->local_port == port && (!addr || !p->local_ip || addr == p->local_ip))
            return true;
    return false;
}

int udp_bind(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    irq_lock_guard guard(g_udp_lock);
    if (pcb->bound) return -22;
    if (!port) {
        for (unsigned i = 0; i < 16384; ++i) {
            port = htons(g_udp_ephemeral++);
            if (g_udp_ephemeral < 49152) g_udp_ephemeral = 49152;
            if (!port_used(pcb, addr, port)) break;
            port = 0;
        }
    }
    if (!port || port_used(pcb, addr, port)) return -98;
    pcb->local_ip = addr;
    pcb->local_port = port;
    pcb->bound = true;
    return 0;
}

int udp_connect(udp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    if (!addr || !port) return -22;
    if (!pcb->bound) {
        int result = udp_bind(pcb, pcb->local_ip, 0);
        if (result) return result;
    }
    irq_lock_guard guard(g_udp_lock);
    pcb->remote_ip = addr;
    pcb->remote_port = port;
    pcb->connected = true;
    return 0;
}

long udp_sendto(udp_pcb* pcb, const void* data, size_t len, uint32_t dst_ip, uint16_t dst_port) noexcept {
    if (!dst_ip || !dst_port) return -89; // EDESTADDRREQ
    auto* iface = dst_ip == 0x0100007F ? netif_loopback() : netif_default();
    if (!iface) return -101;
    if (len > 1472 || len + UDP_HEADER_LEN + IPV4_HEADER_LEN > iface->mtu) return -90;
    if (!pcb->bound) {
        int result = udp_bind(pcb, pcb->local_ip, 0);
        if (result) return result;
    }
    auto* buf = netbuf::alloc();
    if (!buf) return -12;

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
        src = iface->ip.addr;
    }

    ipv4_output(buf, src, dst_ip, IPPROTO_UDP);
    return static_cast<long>(len);
}

long udp_recvfrom(udp_pcb* pcb, void* data, size_t len, uint32_t* src_ip, uint16_t* src_port) noexcept {
    while (true) {
        uintptr_t flags = g_udp_lock.lock();
        if (pcb->recv_head) {
            auto* nb = pcb->recv_head;
            pcb->recv_head = nb->next;
            if (!pcb->recv_head) pcb->recv_tail = nullptr;
            pcb->recv_count--;
            g_udp_lock.unlock(flags);
            size_t to_copy = len < nb->len() ? len : nb->len();
            lib::memcpy(data, nb->data(), to_copy);
            if (src_ip) *src_ip = nb->src_ip;
            if (src_port) *src_port = nb->src_port;
            netbuf::free(nb);
            return static_cast<long>(to_copy);
        }
        auto* current = scheduler::scheduler::current_thread();
        if (pcb->wait_recv && pcb->wait_recv != current) {
            g_udp_lock.unlock(flags);
            return -16;
        }
        pcb->wait_recv = current;
        current->state = scheduler::thread_state::BLOCKED;
        g_udp_lock.unlock(flags);
        scheduler::scheduler::schedule();
    }
}

int udp_poll_events(udp_pcb* pcb) noexcept {
    irq_lock_guard guard(g_udp_lock);
    return 4 | (pcb->recv_head ? 1 : 0);
}

} // namespace kernel::net
