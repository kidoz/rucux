// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/print.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::net {

// ─── Global PCB list ───────────────────────────────────────────────────────

static tcp_pcb* g_pcb_list = nullptr;
static irq_spinlock g_tcp_lock;
static uint16_t g_next_ephemeral = 49152;

static uint32_t g_tcp_isn = 1000; // Simple ISN (should be time-based)

static tcp_pcb* find_pcb(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port) noexcept {
    // Exact match first
    for (auto* p = g_pcb_list; p; p = p->next) {
        if (p->local_port == local_port && p->remote_port == remote_port &&
            (p->local_ip == local_ip || p->local_ip == 0) && p->remote_ip == remote_ip)
            return p;
    }
    // LISTEN match (any remote)
    for (auto* p = g_pcb_list; p; p = p->next) {
        if (p->state == tcp_state::LISTEN && p->local_port == local_port &&
            (p->local_ip == local_ip || p->local_ip == 0))
            return p;
    }
    return nullptr;
}

// ─── TCP segment output ────────────────────────────────────────────────────

static void tcp_send_segment(tcp_pcb* pcb, uint8_t flags, const void* data, size_t data_len) noexcept {
    auto* buf = netbuf::alloc();
    if (!buf) return;

    // Payload
    if (data && data_len > 0) {
        auto* payload = buf->put(data_len);
        lib::memcpy(payload, data, data_len);
    }

    // TCP header
    auto* tcp = reinterpret_cast<tcp_header*>(buf->push(TCP_HEADER_LEN));
    tcp->src_port = pcb->local_port; // Already in network byte order
    tcp->dst_port = pcb->remote_port;
    tcp->seq_num = htonl(pcb->snd_nxt);
    tcp->ack_num = (flags & TCP_ACK) ? htonl(pcb->rcv_nxt) : 0;
    tcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
    tcp->flags = flags;
    tcp->window = htons(static_cast<uint16_t>(pcb->rcv_wnd));
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    // Checksum (includes pseudo-header)
    tcp->checksum = checksum_pseudo(pcb->local_ip, pcb->remote_ip, IPPROTO_TCP, tcp, TCP_HEADER_LEN + data_len);

    // Advance SND.NXT
    if (flags & TCP_SYN) pcb->snd_nxt++;
    if (flags & TCP_FIN) pcb->snd_nxt++;
    pcb->snd_nxt += static_cast<uint32_t>(data_len);

    ipv4_output(buf, pcb->local_ip, pcb->remote_ip, IPPROTO_TCP);
}

static void tcp_send_rst(uint32_t local_ip, uint16_t local_port, uint32_t remote_ip, uint16_t remote_port, uint32_t seq,
                         uint32_t ack) noexcept {
    auto* buf = netbuf::alloc();
    if (!buf) return;

    auto* tcp = reinterpret_cast<tcp_header*>(buf->push(TCP_HEADER_LEN));
    tcp->src_port = local_port;
    tcp->dst_port = remote_port;
    tcp->seq_num = htonl(seq);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = (TCP_HEADER_LEN / 4) << 4;
    tcp->flags = TCP_RST | TCP_ACK;
    tcp->window = 0;
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    tcp->checksum = checksum_pseudo(local_ip, remote_ip, IPPROTO_TCP, tcp, TCP_HEADER_LEN);

    ipv4_output(buf, local_ip, remote_ip, IPPROTO_TCP);
}

// ─── TCP state machine input ───────────────────────────────────────────────

void tcp_input(netif* iface, netbuf* buf) noexcept {
    (void)iface;
    if (buf->len() < TCP_HEADER_LEN) {
        netbuf::free(buf);
        return;
    }

    auto* tcp = reinterpret_cast<tcp_header*>(buf->data());
    size_t hdr_len = (tcp->data_offset >> 4) * 4;
    size_t data_len = buf->len() - hdr_len;
    uint8_t* data = buf->data() + hdr_len;

    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t flags = tcp->flags;

    irq_lock_guard guard(g_tcp_lock);

    auto* pcb = find_pcb(buf->dst_ip, tcp->dst_port, buf->src_ip, tcp->src_port);

    if (!pcb) {
        // No matching PCB — send RST
        if (!(flags & TCP_RST)) {
            tcp_send_rst(buf->dst_ip, tcp->dst_port, buf->src_ip, tcp->src_port, 0,
                         seq + (data_len ? static_cast<uint32_t>(data_len) : 1));
        }
        netbuf::free(buf);
        return;
    }

    switch (pcb->state) {
    case tcp_state::LISTEN: {
        if (flags & TCP_SYN) {
            // Create a new PCB for this connection
            auto* child = tcp_new();
            if (!child || pcb->pending_count >= pcb->backlog) {
                tcp_free(child);
                netbuf::free(buf);
                return;
            }
            child->state = tcp_state::SYN_RECEIVED;
            child->local_ip = buf->dst_ip;
            child->local_port = tcp->dst_port;
            child->remote_ip = buf->src_ip;
            child->remote_port = tcp->src_port;
            child->rcv_nxt = seq + 1;
            child->iss = g_tcp_isn++;
            child->snd_nxt = child->iss;
            child->snd_una = child->iss;
            child->rcv_wnd = child->rcv_buf_size;
            child->listener = pcb;

            // Send SYN+ACK
            tcp_send_segment(child, TCP_SYN | TCP_ACK, nullptr, 0);
        }
        break;
    }

    case tcp_state::SYN_RECEIVED: {
        if (flags & TCP_ACK) {
            pcb->state = tcp_state::ESTABLISHED;
            pcb->snd_una = ack;
            pcb->snd_wnd = ntohs(tcp->window);

            // Move to listener's accept queue
            if (pcb->listener) {
                auto* listener = pcb->listener;
                pcb->next_accept = nullptr;
                if (!listener->accept_queue) {
                    listener->accept_queue = listener->accept_tail = pcb;
                } else {
                    listener->accept_tail->next_accept = pcb;
                    listener->accept_tail = pcb;
                }
                listener->pending_count++;

                // Wake accept()
                if (listener->wait_accept) {
                    scheduler::scheduler::unblock(listener->wait_accept);
                    listener->wait_accept = nullptr;
                }
            }
        }
        break;
    }

    case tcp_state::SYN_SENT: {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            pcb->rcv_nxt = seq + 1;
            pcb->snd_una = ack;
            pcb->snd_wnd = ntohs(tcp->window);
            pcb->state = tcp_state::ESTABLISHED;

            // Send ACK
            tcp_send_segment(pcb, TCP_ACK, nullptr, 0);

            // Wake connect()
            if (pcb->wait_connect) {
                scheduler::scheduler::unblock(pcb->wait_connect);
                pcb->wait_connect = nullptr;
            }
        }
        break;
    }

    case tcp_state::ESTABLISHED: {
        // Handle ACK
        if (flags & TCP_ACK) {
            pcb->snd_una = ack;
            pcb->snd_wnd = ntohs(tcp->window);

            // Wake send() if waiting for window
            if (pcb->wait_send) {
                scheduler::scheduler::unblock(pcb->wait_send);
                pcb->wait_send = nullptr;
            }
        }

        // Handle incoming data
        if (data_len > 0 && seq == pcb->rcv_nxt) {
            // Copy to receive buffer
            size_t space =
                pcb->rcv_buf_size - ((pcb->rcv_buf_head - pcb->rcv_buf_tail + pcb->rcv_buf_size) % pcb->rcv_buf_size);
            size_t to_copy = data_len < space ? data_len : space;

            for (size_t i = 0; i < to_copy; ++i) {
                pcb->rcv_buf[pcb->rcv_buf_head] = data[i];
                pcb->rcv_buf_head = (pcb->rcv_buf_head + 1) % pcb->rcv_buf_size;
            }

            pcb->rcv_nxt += static_cast<uint32_t>(to_copy);
            pcb->rcv_wnd = static_cast<uint32_t>(space - to_copy);

            // Send ACK
            tcp_send_segment(pcb, TCP_ACK, nullptr, 0);

            // Wake recv()
            if (pcb->wait_recv) {
                scheduler::scheduler::unblock(pcb->wait_recv);
                pcb->wait_recv = nullptr;
            }
        }

        // Handle FIN
        if (flags & TCP_FIN) {
            pcb->rcv_nxt = seq + static_cast<uint32_t>(data_len) + 1;
            pcb->state = tcp_state::CLOSE_WAIT;
            tcp_send_segment(pcb, TCP_ACK, nullptr, 0);

            // Wake recv() with EOF
            if (pcb->wait_recv) {
                scheduler::scheduler::unblock(pcb->wait_recv);
                pcb->wait_recv = nullptr;
            }
        }
        break;
    }

    case tcp_state::FIN_WAIT_1: {
        if (flags & TCP_ACK) {
            pcb->snd_una = ack;
            if (flags & TCP_FIN) {
                pcb->rcv_nxt = seq + 1;
                pcb->state = tcp_state::TIME_WAIT;
                tcp_send_segment(pcb, TCP_ACK, nullptr, 0);
            } else {
                pcb->state = tcp_state::FIN_WAIT_2;
            }
        }
        break;
    }

    case tcp_state::FIN_WAIT_2: {
        if (flags & TCP_FIN) {
            pcb->rcv_nxt = seq + 1;
            pcb->state = tcp_state::TIME_WAIT;
            tcp_send_segment(pcb, TCP_ACK, nullptr, 0);
        }
        break;
    }

    case tcp_state::LAST_ACK: {
        if (flags & TCP_ACK) {
            pcb->state = tcp_state::CLOSED;
        }
        break;
    }

    default:
        break;
    }

    netbuf::free(buf);
}

// ─── TCP API ───────────────────────────────────────────────────────────────

static constexpr size_t TCP_BUF_SIZE = 32768;

tcp_pcb* tcp_new() noexcept {
    auto* pcb = new tcp_pcb();
    if (!pcb) return nullptr;

    lib::memset(pcb, 0, sizeof(tcp_pcb));
    pcb->state = tcp_state::CLOSED;
    pcb->rcv_buf = new uint8_t[TCP_BUF_SIZE];
    pcb->rcv_buf_size = TCP_BUF_SIZE;
    pcb->rcv_wnd = TCP_BUF_SIZE;
    pcb->snd_wnd = TCP_BUF_SIZE;

    irq_lock_guard guard(g_tcp_lock);
    pcb->next = g_pcb_list;
    g_pcb_list = pcb;
    return pcb;
}

void tcp_free(tcp_pcb* pcb) noexcept {
    if (!pcb) return;

    // Remove from global list
    irq_lock_guard guard(g_tcp_lock);
    tcp_pcb** pp = &g_pcb_list;
    while (*pp) {
        if (*pp == pcb) {
            *pp = pcb->next;
            break;
        }
        pp = &(*pp)->next;
    }

    delete[] pcb->rcv_buf;
    delete[] pcb->snd_buf;
    delete pcb;
}

int tcp_bind(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    pcb->local_ip = addr;
    pcb->local_port = port;
    return 0;
}

int tcp_listen(tcp_pcb* pcb, int backlog) noexcept {
    pcb->state = tcp_state::LISTEN;
    pcb->backlog = backlog;
    return 0;
}

tcp_pcb* tcp_accept(tcp_pcb* pcb) noexcept {
    while (true) {
        uintptr_t flags = pcb->lock.lock();
        if (pcb->accept_queue) {
            auto* child = pcb->accept_queue;
            pcb->accept_queue = child->next_accept;
            if (!pcb->accept_queue) pcb->accept_tail = nullptr;
            pcb->pending_count--;
            child->listener = nullptr;
            pcb->lock.unlock(flags);
            return child;
        }
        // Block
        pcb->wait_accept = scheduler::scheduler::current_thread();
        pcb->lock.unlock(flags);
        scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
    }
}

int tcp_connect(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    pcb->remote_ip = addr;
    pcb->remote_port = port;

    // Assign ephemeral port if not bound
    if (pcb->local_port == 0) pcb->local_port = htons(g_next_ephemeral++);

    // Get source IP from the outgoing interface
    if (pcb->local_ip == 0) {
        auto* iface = netif_default();
        if (iface) pcb->local_ip = iface->ip.addr;
    }

    pcb->iss = g_tcp_isn++;
    pcb->snd_nxt = pcb->iss;
    pcb->snd_una = pcb->iss;
    pcb->state = tcp_state::SYN_SENT;

    // Send SYN
    tcp_send_segment(pcb, TCP_SYN, nullptr, 0);

    // Block until ESTABLISHED or error
    pcb->wait_connect = scheduler::scheduler::current_thread();
    scheduler::scheduler::block(scheduler::thread_state::BLOCKED);

    return (pcb->state == tcp_state::ESTABLISHED) ? 0 : -1;
}

long tcp_send(tcp_pcb* pcb, const void* data, size_t len) noexcept {
    if (pcb->state != tcp_state::ESTABLISHED) return -1;

    auto* p = reinterpret_cast<const uint8_t*>(data);
    size_t sent = 0;

    while (sent < len) {
        size_t chunk = len - sent;
        uint16_t mss = 1460; // Standard MSS for Ethernet
        if (chunk > mss) chunk = mss;

        tcp_send_segment(pcb, TCP_ACK | TCP_PSH, p + sent, chunk);
        sent += chunk;
    }

    return static_cast<long>(sent);
}

long tcp_recv(tcp_pcb* pcb, void* data, size_t len) noexcept {
    auto* p = reinterpret_cast<uint8_t*>(data);

    // Block until data available or connection closed
    while (pcb->rcv_buf_head == pcb->rcv_buf_tail) {
        if (pcb->state == tcp_state::CLOSE_WAIT || pcb->state == tcp_state::CLOSED) return 0; // EOF

        pcb->wait_recv = scheduler::scheduler::current_thread();
        scheduler::scheduler::block(scheduler::thread_state::BLOCKED);
    }

    // Copy from receive buffer
    size_t avail = (pcb->rcv_buf_head - pcb->rcv_buf_tail + pcb->rcv_buf_size) % pcb->rcv_buf_size;
    size_t to_read = (len < avail) ? len : avail;

    for (size_t i = 0; i < to_read; ++i) {
        p[i] = pcb->rcv_buf[pcb->rcv_buf_tail];
        pcb->rcv_buf_tail = (pcb->rcv_buf_tail + 1) % pcb->rcv_buf_size;
    }

    // Update receive window
    pcb->rcv_wnd += static_cast<uint32_t>(to_read);

    return static_cast<long>(to_read);
}

int tcp_close(tcp_pcb* pcb) noexcept {
    if (pcb->state == tcp_state::ESTABLISHED) {
        pcb->state = tcp_state::FIN_WAIT_1;
        tcp_send_segment(pcb, TCP_FIN | TCP_ACK, nullptr, 0);
    } else if (pcb->state == tcp_state::CLOSE_WAIT) {
        pcb->state = tcp_state::LAST_ACK;
        tcp_send_segment(pcb, TCP_FIN | TCP_ACK, nullptr, 0);
    }
    return 0;
}

int tcp_poll_events(tcp_pcb* pcb) noexcept {
    int events = 0;
    if (pcb->rcv_buf_head != pcb->rcv_buf_tail || pcb->state == tcp_state::CLOSE_WAIT ||
        pcb->state == tcp_state::CLOSED)
        events |= 1;                                       // POLLIN
    if (pcb->state == tcp_state::ESTABLISHED) events |= 4; // POLLOUT
    return events;
}

void tcp_init() noexcept {
    g_pcb_list = nullptr;
    g_next_ephemeral = 49152;
}

} // namespace kernel::net
