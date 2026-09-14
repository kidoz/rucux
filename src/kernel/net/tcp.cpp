// SPDX-License-Identifier: MIT
#include <kernel/net/ipv4.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/time.hpp>
#include <knew.hpp>
#include <lib/string.hpp>

namespace kernel::net {
namespace {
constexpr size_t TCP_BUF_SIZE = 32768;
constexpr unsigned MAX_PCBS = 128;
constexpr unsigned MAX_DATA_SEGMENTS = 8, MAX_RETRIES = 4;
constexpr uint32_t LOCAL_MSS = 1460, DEFAULT_MSS = 536;
constexpr uint64_t INITIAL_RTO = 1000, MAX_RTO = 60000;
constexpr uint64_t MAX_PERSIST_DELAY = 4000, PERSIST_SILENCE_LIMIT = 15000;
static tcp_pcb* g_pcb_list;
static irq_spinlock g_tcp_lock;
static uint16_t g_next_ephemeral = 49152;
static uint32_t g_tcp_isn = 1000;
static unsigned g_count;
static netbuf* g_output_head;
static netbuf* g_output_tail;
static bool g_flushing_output;

// Output may synchronously enter TCP through loopback. Never transmit while
// holding the PCB lock; the queue owns packets independently of their PCB.
static void flush_output() noexcept {
    {
        irq_lock_guard guard(g_tcp_lock);
        if (g_flushing_output) return;
        g_flushing_output = true;
    }
    // One drainer also prevents loopback responses from recursively growing the stack.
    while (true) {
        auto saved = g_tcp_lock.lock();
        auto* buf = g_output_head;
        if (buf) {
            g_output_head = buf->next;
            if (!g_output_head) g_output_tail = nullptr;
        } else
            g_flushing_output = false;
        g_tcp_lock.unlock(saved);
        if (!buf) return;
        ipv4_output(buf, buf->src_ip, buf->dst_ip, IPPROTO_TCP);
    }
}
struct OutputGuard {
    ~OutputGuard() { flush_output(); }
};

static void wake(scheduler::thread*& waiter) noexcept {
    auto* thread = waiter;
    waiter = nullptr;
    if (thread && thread->state == scheduler::thread_state::BLOCKED) scheduler::scheduler::unblock(thread);
}

static void enqueue_output(netbuf* buf) noexcept {
    if (g_output_tail)
        g_output_tail->next = buf;
    else
        g_output_head = buf;
    g_output_tail = buf;
}

static netbuf* copy_segment(tcp_pcb* pcb, netbuf* original, size_t limit = LOCAL_MSS) noexcept {
    auto* copy = netbuf::alloc();
    if (!copy) return nullptr;
    lib::memcpy(copy->put(original->len()), original->data(), original->len());
    copy->src_ip = pcb->local_ip;
    copy->dst_ip = pcb->remote_ip;
    auto* header = reinterpret_cast<tcp_header*>(copy->data());
    const size_t header_length = (header->data_offset >> 4U) * 4;
    if (copy->len() - header_length > limit) {
        copy->set_len(header_length + limit);
        header->flags = static_cast<uint8_t>(header->flags & (0xFFU ^ TCP_FIN));
    }
    // Preserve sequence/payload, but advertise current ACK and receive window.
    if (header->flags & TCP_ACK) header->ack_num = htonl(pcb->rcv_nxt);
    header->window = htons(static_cast<uint16_t>(pcb->rcv_wnd));
    header->checksum = 0;
    header->checksum = checksum_pseudo(copy->src_ip, copy->dst_ip, IPPROTO_TCP, header, copy->len());
    return copy;
}

static void clear_unacked(tcp_pcb* pcb) noexcept {
    while (pcb->unacked_head) {
        auto* next = pcb->unacked_head->next;
        netbuf::free(pcb->unacked_head);
        pcb->unacked_head = next;
    }
    pcb->unacked_tail = nullptr;
    pcb->unacked_count = 0;
    pcb->retransmit_at = 0;
    pcb->retries = 0;
    pcb->rtt_pending = pcb->rtt_blocked = false;
    pcb->persist_at = pcb->persist_expires = 0;
    pcb->fin_pending = false;
}

void invalidate_rtt(tcp_pcb* pcb) noexcept {
    // Karn's rule: even a later cumulative ACK may cover retransmitted bytes.
    pcb->rtt_pending = false;
    pcb->rtt_blocked = pcb->snd_una != pcb->snd_nxt;
    pcb->rtt_block_until = pcb->snd_nxt;
}

void update_rtt(tcp_pcb* pcb, uint32_t ack, uint64_t now) noexcept {
    if (pcb->rtt_pending && static_cast<int32_t>(ack - pcb->rtt_end) >= 0) {
        uint64_t elapsed = now - pcb->rtt_started;
        if (!elapsed) elapsed = 1;
        uint32_t sample = static_cast<uint32_t>(elapsed > MAX_RTO ? MAX_RTO : elapsed);
        if (!pcb->srtt) {
            pcb->srtt = sample;
            pcb->rttvar = sample / 2;
        } else {
            uint32_t delta = pcb->srtt > sample ? pcb->srtt - sample : sample - pcb->srtt;
            pcb->rttvar = (3 * pcb->rttvar + delta) / 4;
            pcb->srtt = (7 * pcb->srtt + sample) / 8;
        }
        uint64_t variance = 4 * pcb->rttvar;
        pcb->rto = pcb->srtt + (variance > 100 ? variance : 100); // Timer granularity.
        if (pcb->rto < INITIAL_RTO) pcb->rto = INITIAL_RTO;
        if (pcb->rto > MAX_RTO) pcb->rto = MAX_RTO;
        pcb->rtt_pending = false;
    }
    if (pcb->rtt_blocked && static_cast<int32_t>(ack - pcb->rtt_block_until) >= 0) pcb->rtt_blocked = false;
}

void grow_congestion_window(tcp_pcb* pcb, uint32_t bytes) noexcept {
    if (!bytes) return;
    // Limit ACK splitting to one MSS of credit per ACK and one MSS per window
    // during congestion avoidance. The retained-packet queue remains the hard cap.
    if (bytes > pcb->send_mss) bytes = pcb->send_mss;
    if (pcb->cwnd < pcb->ssthresh) {
        pcb->cwnd += bytes;
    } else {
        pcb->congestion_credit += bytes;
        if (pcb->congestion_credit >= pcb->cwnd) {
            pcb->congestion_credit -= pcb->cwnd;
            pcb->cwnd += pcb->send_mss;
        }
    }
    if (pcb->cwnd > MAX_DATA_SEGMENTS * pcb->send_mss) pcb->cwnd = MAX_DATA_SEGMENTS * pcb->send_mss;
}

void update_window(tcp_pcb* pcb, uint32_t window) noexcept {
    const uint64_t now = time_manager::get_ticks();
    bool was_zero = !pcb->snd_wnd;
    pcb->snd_wnd = window;
    if (!window) {
        if (!pcb->persist_at) {
            pcb->persist_delay = INITIAL_RTO;
            pcb->persist_at = now + pcb->persist_delay;
            invalidate_rtt(pcb);
        }
        pcb->persist_expires = now + PERSIST_SILENCE_LIMIT;
    } else {
        pcb->persist_at = pcb->persist_expires = 0;
        if (was_zero && pcb->unacked_head) {
            size_t limit = window < pcb->send_mss ? window : pcb->send_mss;
            auto* retry = copy_segment(pcb, pcb->unacked_head, limit);
            if (retry) enqueue_output(retry);
            pcb->retransmit_at = now + pcb->rto;
        }
    }
}

static void acknowledge(tcp_pcb* pcb, uint32_t ack) noexcept {
    if (static_cast<int32_t>(ack - pcb->snd_una) <= 0) return;
    update_rtt(pcb, ack, time_manager::get_ticks());
    pcb->snd_una = ack;
    uint32_t acknowledged_data = 0;
    while (auto* pending = pcb->unacked_head) {
        auto* header = reinterpret_cast<tcp_header*>(pending->data());
        uint32_t seq = ntohl(header->seq_num);
        const size_t header_length = (header->data_offset >> 4U) * 4;
        size_t payload = pending->len() - header_length;
        if (!(header->flags & TCP_SYN) && static_cast<int32_t>(ack - seq) > 0)
            acknowledged_data += ack - seq < payload ? ack - seq : static_cast<uint32_t>(payload);
        uint32_t end = seq + static_cast<uint32_t>(payload) + !!(header->flags & TCP_SYN) + !!(header->flags & TCP_FIN);
        if (static_cast<int32_t>(ack - end) >= 0) {
            pcb->unacked_head = pending->next;
            netbuf::free(pending);
            --pcb->unacked_count;
            continue;
        }
        // Keep only the unacknowledged suffix, including a pending FIN.
        if (static_cast<int32_t>(ack - seq) > 0 && !(header->flags & TCP_SYN)) {
            size_t trim = ack - seq;
            if (trim <= payload) {
                lib::memmove(pending->data() + header_length, pending->data() + header_length + trim, payload - trim);
                pending->set_len(pending->len() - trim);
                header->seq_num = htonl(ack);
            }
        }
        break;
    }
    grow_congestion_window(pcb, acknowledged_data);
    if (!pcb->unacked_head) pcb->unacked_tail = nullptr;
    pcb->retries = 0;
    pcb->retransmit_at = pcb->unacked_head ? time_manager::get_ticks() + pcb->rto : 0;
}

static bool send_segment(tcp_pcb* pcb, uint8_t flags, const void* data = nullptr, size_t length = 0,
                         bool probe = false) noexcept {
    bool reliable = length || (flags & (TCP_SYN | TCP_FIN));
    // One extra slot is reserved for close()'s FIN after queued data.
    if (reliable && pcb->unacked_count >= MAX_DATA_SEGMENTS + !!(flags & TCP_FIN)) return false;
    auto* buf = netbuf::alloc();
    if (!buf) return false;
    auto* payload = buf->put(length);
    if (!payload) {
        netbuf::free(buf);
        return false;
    }
    if (length) lib::memcpy(payload, data, length);
    const size_t header_length = TCP_HEADER_LEN + ((flags & TCP_SYN) ? 4 : 0);
    auto* tcp = reinterpret_cast<tcp_header*>(buf->push(header_length));
    if (flags & TCP_SYN) {
        auto* option = buf->data() + TCP_HEADER_LEN;
        option[0] = 2;
        option[1] = 4;
        option[2] = LOCAL_MSS >> 8U;
        option[3] = LOCAL_MSS & 255U;
    }
    tcp->src_port = pcb->local_port;
    tcp->dst_port = pcb->remote_port;
    tcp->seq_num = htonl(probe ? pcb->snd_una - 1 : pcb->snd_nxt);
    tcp->ack_num = (flags & TCP_ACK) ? htonl(pcb->rcv_nxt) : 0;
    tcp->data_offset = static_cast<uint8_t>((header_length / 4) << 4U);
    tcp->flags = flags;
    tcp->window = htons(static_cast<uint16_t>(pcb->rcv_wnd));
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
    tcp->checksum = checksum_pseudo(pcb->local_ip, pcb->remote_ip, IPPROTO_TCP, tcp, buf->len());
    buf->src_ip = pcb->local_ip;
    buf->dst_ip = pcb->remote_ip;
    netbuf* wire = buf;
    if (reliable) {
        wire = copy_segment(pcb, buf);
        if (!wire) {
            netbuf::free(buf);
            return false;
        }
        if (pcb->unacked_tail)
            pcb->unacked_tail->next = buf;
        else {
            pcb->unacked_head = buf;
            pcb->retransmit_at = time_manager::get_ticks() + pcb->rto;
            pcb->retries = 0;
        }
        pcb->unacked_tail = buf;
        ++pcb->unacked_count;
    }
    pcb->snd_nxt += static_cast<uint32_t>(length) + !!(flags & TCP_SYN) + !!(flags & TCP_FIN);
    if (reliable && !pcb->rtt_pending && !pcb->rtt_blocked) {
        pcb->rtt_pending = true;
        pcb->rtt_started = time_manager::get_ticks();
        pcb->rtt_end = pcb->snd_nxt;
    }
    enqueue_output(wire);
    return true;
}

void send_pending_fin(tcp_pcb* pcb) noexcept {
    const uint32_t flight = pcb->snd_nxt - pcb->snd_una;
    if (pcb->fin_pending && flight < pcb->snd_wnd && flight < pcb->cwnd && send_segment(pcb, TCP_FIN | TCP_ACK))
        pcb->fin_pending = false;
}

static tcp_pcb* new_locked() noexcept {
    if (g_count >= MAX_PCBS) return nullptr;
    auto* pcb = new tcp_pcb();
    if (!pcb) return nullptr;
    pcb->rcv_buf = new uint8_t[TCP_BUF_SIZE];
    if (!pcb->rcv_buf) {
        delete pcb;
        return nullptr;
    }
    pcb->send_mss = pcb->cwnd = DEFAULT_MSS;
    pcb->ssthresh = MAX_DATA_SEGMENTS * DEFAULT_MSS;
    pcb->rto = INITIAL_RTO;
    pcb->rcv_buf_size = TCP_BUF_SIZE;
    pcb->rcv_wnd = TCP_BUF_SIZE - 1; // Reserve one slot to distinguish full/empty.
    pcb->next = g_pcb_list;
    g_pcb_list = pcb;
    ++g_count;
    return pcb;
}

static void free_locked(tcp_pcb* pcb) noexcept {
    if (pcb->listener) {
        auto* listener = pcb->listener;
        tcp_pcb** q = &listener->accept_queue;
        tcp_pcb* previous = nullptr;
        while (*q && *q != pcb) {
            previous = *q;
            q = &(*q)->next_accept;
        }
        if (*q) {
            *q = pcb->next_accept;
            if (listener->accept_tail == pcb) listener->accept_tail = previous;
        }
        --listener->pending_count;
    }
    auto** link = &g_pcb_list;
    while (*link && *link != pcb)
        link = &(*link)->next;
    if (*link) {
        *link = pcb->next;
        --g_count;
    }
    delete[] pcb->rcv_buf;
    clear_unacked(pcb);
    delete pcb;
}

static void reset_locked(tcp_pcb* pcb) noexcept {
    clear_unacked(pcb);
    pcb->state = tcp_state::CLOSED;
    pcb->error = -104;
    wake(pcb->wait_connect);
    wake(pcb->wait_recv);
    wake(pcb->wait_send);
}

static tcp_pcb* find_pcb(uint32_t local, uint16_t port, uint32_t remote, uint16_t remote_port) noexcept {
    for (auto* p = g_pcb_list; p; p = p->next)
        if (p->state != tcp_state::CLOSED && p->state != tcp_state::LISTEN && p->local_port == port &&
            p->remote_port == remote_port && p->remote_ip == remote && (!p->local_ip || p->local_ip == local))
            return p;
    for (auto* p = g_pcb_list; p; p = p->next)
        if (p->state == tcp_state::LISTEN && p->local_port == port && (!p->local_ip || p->local_ip == local)) return p;
    return nullptr;
}

static bool port_used(tcp_pcb* self, uint32_t addr, uint16_t port) noexcept {
    for (auto* p = g_pcb_list; p; p = p->next)
        if (p != self && p->local_port == port && (!addr || !p->local_ip || p->local_ip == addr)) return true;
    return false;
}

static int bind_locked(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    if (pcb->state != tcp_state::CLOSED || pcb->local_port) return -22;
    if (!port) {
        for (unsigned i = 0; i < 16384; ++i) {
            port = htons(g_next_ephemeral++);
            if (g_next_ephemeral < 49152) g_next_ephemeral = 49152;
            if (!port_used(pcb, addr, port)) break;
            port = 0;
        }
    }
    if (!port || port_used(pcb, addr, port)) return -98;
    pcb->local_ip = addr;
    pcb->local_port = port;
    return 0;
}
bool parse_options(const uint8_t* bytes, size_t header_length, uint32_t& mss) noexcept {
    mss = DEFAULT_MSS;
    bool seen_mss = false;
    for (size_t pos = TCP_HEADER_LEN; pos < header_length;) {
        const uint8_t kind = bytes[pos++];
        if (!kind) break;
        if (kind == 1) continue;
        if (pos >= header_length) return false;
        const uint8_t length = bytes[pos++];
        if (length < 2 || length - 2U > header_length - pos) return false;
        if (kind == 2) {
            if (length != 4 || seen_mss) return false;
            uint32_t value = (static_cast<uint32_t>(bytes[pos]) << 8U) | bytes[pos + 1];
            if (!value) return false;
            mss = value < LOCAL_MSS ? value : LOCAL_MSS;
            seen_mss = true;
        }
        pos += length - 2;
    }
    return true;
}

void set_mss(tcp_pcb* pcb, uint32_t mss) noexcept {
    pcb->send_mss = pcb->cwnd = mss; // Conservative one-segment initial window.
    pcb->ssthresh = MAX_DATA_SEGMENTS * mss;
    pcb->congestion_credit = 0;
}
} // namespace

void tcp_input(netif*, netbuf* buf) noexcept {
    if (buf->len() < TCP_HEADER_LEN) {
        netbuf::free(buf);
        return;
    }
    auto* tcp = reinterpret_cast<tcp_header*>(buf->data());
    const size_t header = (tcp->data_offset >> 4) * 4;
    if (header < TCP_HEADER_LEN || header > buf->len() ||
        checksum_pseudo(buf->src_ip, buf->dst_ip, IPPROTO_TCP, tcp, buf->len())) {
        netbuf::free(buf);
        return;
    }
    uint32_t mss;
    if (!parse_options(buf->data(), header, mss)) {
        netbuf::free(buf);
        return;
    }
    const size_t length = buf->len() - header;
    const auto* data = buf->data() + header;
    const uint32_t seq = ntohl(tcp->seq_num), ack = ntohl(tcp->ack_num);
    const uint8_t flags = tcp->flags;
    OutputGuard output;
    irq_lock_guard guard(g_tcp_lock);
    auto* pcb = find_pcb(buf->dst_ip, tcp->dst_port, buf->src_ip, tcp->src_port);
    if (!pcb) {
        if (!(flags & TCP_RST)) {
            tcp_pcb reply{};
            reply.local_ip = buf->dst_ip;
            reply.remote_ip = buf->src_ip;
            reply.local_port = tcp->dst_port;
            reply.remote_port = tcp->src_port;
            reply.snd_nxt = (flags & TCP_ACK) ? ack : 0;
            reply.rcv_nxt = seq + static_cast<uint32_t>(length) + !!(flags & TCP_SYN) + !!(flags & TCP_FIN);
            send_segment(&reply, TCP_RST | ((flags & TCP_ACK) ? 0 : TCP_ACK));
        }
        netbuf::free(buf);
        return;
    }
    if (flags & TCP_RST) {
        if ((pcb->state == tcp_state::SYN_SENT && (flags & TCP_ACK) && ack == pcb->snd_nxt) ||
            (pcb->state != tcp_state::LISTEN && seq == pcb->rcv_nxt)) {
            reset_locked(pcb);
            if (pcb->detached || pcb->listener) free_locked(pcb);
        }
        netbuf::free(buf);
        return;
    }
    if (pcb->state == tcp_state::LISTEN) {
        if ((flags & (TCP_SYN | TCP_ACK | TCP_FIN)) == TCP_SYN && pcb->pending_count < pcb->backlog) {
            auto* child = new_locked();
            if (child) {
                set_mss(child, mss);
                child->snd_wnd = ntohs(tcp->window);
                child->state = tcp_state::SYN_RECEIVED;
                child->local_ip = buf->dst_ip;
                child->remote_ip = buf->src_ip;
                child->local_port = tcp->dst_port;
                child->remote_port = tcp->src_port;
                child->rcv_nxt = seq + 1;
                child->iss = g_tcp_isn += 65536;
                child->snd_una = child->snd_nxt = child->iss;
                child->listener = pcb;
                child->expires = time_manager::get_ticks() + 15000;
                ++pcb->pending_count; // Includes incomplete handshakes.
                if (!send_segment(child, TCP_SYN | TCP_ACK)) free_locked(child);
            }
        }
        netbuf::free(buf);
        return;
    }
    if (pcb->state == tcp_state::SYN_SENT) {
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && ack == pcb->snd_nxt) {
            set_mss(pcb, mss);
            pcb->rcv_nxt = seq + 1;
            acknowledge(pcb, ack);
            update_window(pcb, ntohs(tcp->window));
            pcb->state = tcp_state::ESTABLISHED;
            pcb->expires = 0;
            send_segment(pcb, TCP_ACK);
            wake(pcb->wait_connect);
        }
        netbuf::free(buf);
        return;
    }
    if (pcb->state == tcp_state::SYN_RECEIVED) {
        if ((flags & TCP_SYN) && seq + 1 == pcb->rcv_nxt) {
            if (pcb->unacked_head) {
                invalidate_rtt(pcb);
                auto* retry = copy_segment(pcb, pcb->unacked_head);
                if (retry) enqueue_output(retry);
            }
        } else if ((flags & TCP_ACK) && ack == pcb->snd_nxt && seq == pcb->rcv_nxt) {
            pcb->state = tcp_state::ESTABLISHED;
            pcb->expires = time_manager::get_ticks() + 5000; // Also bound unaccepted connections.
            auto* listener = pcb->listener;
            if (listener->accept_tail)
                listener->accept_tail->next_accept = pcb;
            else
                listener->accept_queue = pcb;
            listener->accept_tail = pcb;
            wake(listener->wait_accept);
        } else {
            netbuf::free(buf);
            return;
        }
        if (pcb->state != tcp_state::ESTABLISHED) {
            netbuf::free(buf);
            return;
        }
    }
    // This small stack accepts only in-order segments and acknowledges duplicates.
    if (seq != pcb->rcv_nxt || (flags & TCP_SYN)) {
        send_segment(pcb, TCP_ACK);
        netbuf::free(buf);
        return;
    }
    if (!(flags & TCP_ACK) || static_cast<int32_t>(ack - pcb->snd_nxt) > 0) {
        netbuf::free(buf);
        return;
    }
    if (static_cast<int32_t>(ack - pcb->snd_una) >= 0) {
        acknowledge(pcb, ack);
        update_window(pcb, ntohs(tcp->window));
        wake(pcb->wait_send);
    }
    send_pending_fin(pcb);
    if (pcb->state == tcp_state::LAST_ACK && !pcb->fin_pending && ack == pcb->snd_nxt) {
        pcb->state = tcp_state::CLOSED;
        if (pcb->detached) free_locked(pcb);
        netbuf::free(buf);
        return;
    }
    if (pcb->state == tcp_state::FIN_WAIT_1 && !pcb->fin_pending && ack == pcb->snd_nxt)
        pcb->state = tcp_state::FIN_WAIT_2;
    if (pcb->state == tcp_state::CLOSING && !pcb->fin_pending && ack == pcb->snd_nxt) {
        pcb->state = tcp_state::TIME_WAIT;
        pcb->expires = time_manager::get_ticks() + 2000;
    }
    if (pcb->state == tcp_state::ESTABLISHED || pcb->state == tcp_state::FIN_WAIT_1 ||
        pcb->state == tcp_state::FIN_WAIT_2) {
        size_t copied = length < pcb->rcv_wnd ? length : pcb->rcv_wnd;
        if (pcb->detached)
            copied = length; // Closed application no longer consumes data.
        else
            for (size_t i = 0; i < copied; ++i) {
                pcb->rcv_buf[pcb->rcv_buf_head] = data[i];
                pcb->rcv_buf_head = (pcb->rcv_buf_head + 1) % pcb->rcv_buf_size;
            }
        pcb->rcv_nxt += static_cast<uint32_t>(copied);
        if (!pcb->detached) pcb->rcv_wnd -= static_cast<uint32_t>(copied);
        if ((flags & TCP_FIN) && copied == length) {
            ++pcb->rcv_nxt;
            if (pcb->state == tcp_state::ESTABLISHED)
                pcb->state = tcp_state::CLOSE_WAIT;
            else if (pcb->state == tcp_state::FIN_WAIT_1)
                pcb->state = tcp_state::CLOSING;
            else {
                pcb->state = tcp_state::TIME_WAIT;
                pcb->expires = time_manager::get_ticks() + 2000;
            }
        }
        if (length || (flags & TCP_FIN)) {
            send_segment(pcb, TCP_ACK);
            wake(pcb->wait_recv);
        }
    }
    netbuf::free(buf);
}

tcp_pcb* tcp_new() noexcept {
    irq_lock_guard guard(g_tcp_lock);
    return new_locked();
}

void tcp_free(tcp_pcb* pcb) noexcept {
    if (!pcb) return;
    irq_lock_guard guard(g_tcp_lock);
    free_locked(pcb);
}

int tcp_bind(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    irq_lock_guard guard(g_tcp_lock);
    return bind_locked(pcb, addr, port);
}

int tcp_listen(tcp_pcb* pcb, int backlog) noexcept {
    irq_lock_guard guard(g_tcp_lock);
    if (pcb->state != tcp_state::CLOSED || !pcb->local_port) return -22;
    pcb->state = tcp_state::LISTEN;
    pcb->backlog = backlog < 1 ? 1 : (backlog > 16 ? 16 : backlog);
    return 0;
}

tcp_pcb* tcp_accept(tcp_pcb* pcb) noexcept {
    while (true) {
        auto saved = g_tcp_lock.lock();
        if (pcb->state != tcp_state::LISTEN) {
            g_tcp_lock.unlock(saved);
            return nullptr;
        }
        if (auto* child = pcb->accept_queue) {
            pcb->accept_queue = child->next_accept;
            if (!pcb->accept_queue) pcb->accept_tail = nullptr;
            --pcb->pending_count;
            child->listener = nullptr;
            child->next_accept = nullptr;
            child->expires = 0;
            g_tcp_lock.unlock(saved);
            return child;
        }
        auto* current = scheduler::scheduler::current_thread();
        if (pcb->wait_accept && pcb->wait_accept != current) {
            g_tcp_lock.unlock(saved);
            return nullptr;
        }
        pcb->wait_accept = current;
        current->state = scheduler::thread_state::BLOCKED;
        g_tcp_lock.unlock(saved);
        scheduler::scheduler::schedule();
    }
}

int tcp_connect(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept {
    {
        OutputGuard output;
        irq_lock_guard guard(g_tcp_lock);
        if (pcb->state != tcp_state::CLOSED || !addr || !port) return -22;
        auto* iface = addr == 0x0100007F ? netif_loopback() : netif_default();
        if (!iface) return -101;
        if (!pcb->local_port) {
            auto result = bind_locked(pcb, pcb->local_ip, 0);
            if (result) return result;
        }
        if (!pcb->local_ip) pcb->local_ip = iface->ip.addr;
        pcb->remote_ip = addr;
        pcb->remote_port = port;
        pcb->iss = g_tcp_isn += 65536;
        pcb->snd_nxt = pcb->snd_una = pcb->iss;
        pcb->state = tcp_state::SYN_SENT;
        pcb->expires = time_manager::get_ticks() + 15000;
        pcb->error = 0;
        if (!send_segment(pcb, TCP_SYN)) {
            pcb->state = tcp_state::CLOSED;
            return -12;
        }
    }
    while (true) {
        auto saved = g_tcp_lock.lock();
        if (pcb->state != tcp_state::SYN_SENT) {
            int result = pcb->state == tcp_state::ESTABLISHED ? 0 : (pcb->error ? pcb->error : -111);
            g_tcp_lock.unlock(saved);
            return result;
        }
        pcb->wait_connect = scheduler::scheduler::current_thread();
        pcb->wait_connect->state = scheduler::thread_state::BLOCKED;
        g_tcp_lock.unlock(saved);
        scheduler::scheduler::schedule();
    }
}

long tcp_send(tcp_pcb* pcb, const void* data, size_t len) noexcept {
    OutputGuard output;
    irq_lock_guard guard(g_tcp_lock);
    if (pcb->state != tcp_state::ESTABLISHED && pcb->state != tcp_state::CLOSE_WAIT) return -107;
    if (!len) return 0;
    if (pcb->unacked_count >= MAX_DATA_SEGMENTS) return -11;
    const uint64_t now = time_manager::get_ticks();
    if (pcb->snd_nxt == pcb->snd_una && now - pcb->last_data_sent >= pcb->rto) {
        pcb->cwnd = pcb->send_mss;
        pcb->congestion_credit = 0;
    }
    // Bound each syscall to the negotiated MSS, congestion and peer windows.
    size_t flight = pcb->snd_nxt - pcb->snd_una;
    uint32_t window = pcb->snd_wnd < pcb->cwnd ? pcb->snd_wnd : pcb->cwnd;
    size_t available = flight < window ? window - flight : 0;
    size_t count = len < pcb->send_mss ? len : pcb->send_mss;
    if (count > available) count = available;
    if (!count) return -11;
    if (!send_segment(pcb, TCP_ACK | TCP_PSH, data, count)) return -12;
    pcb->last_data_sent = now;
    return static_cast<long>(count);
}

long tcp_recv(tcp_pcb* pcb, void* data, size_t len) noexcept {
    if (!len) return 0;
    while (true) {
        auto saved = g_tcp_lock.lock();
        size_t available = (pcb->rcv_buf_head + pcb->rcv_buf_size - pcb->rcv_buf_tail) % pcb->rcv_buf_size;
        if (available) {
            size_t count = len < available ? len : available;
            for (size_t i = 0; i < count; ++i) {
                static_cast<uint8_t*>(data)[i] = pcb->rcv_buf[pcb->rcv_buf_tail];
                pcb->rcv_buf_tail = (pcb->rcv_buf_tail + 1) % pcb->rcv_buf_size;
            }
            pcb->rcv_wnd += static_cast<uint32_t>(count);
            send_segment(pcb, TCP_ACK); // Advertise reopened receive window.
            g_tcp_lock.unlock(saved);
            flush_output();
            return static_cast<long>(count);
        }
        if (pcb->state != tcp_state::ESTABLISHED) {
            long result = pcb->error ? pcb->error : (pcb->state == tcp_state::CLOSE_WAIT ? 0 : -107);
            g_tcp_lock.unlock(saved);
            return result;
        }
        auto* current = scheduler::scheduler::current_thread();
        if (pcb->wait_recv && pcb->wait_recv != current) {
            g_tcp_lock.unlock(saved);
            return -16;
        }
        pcb->wait_recv = current;
        current->state = scheduler::thread_state::BLOCKED;
        g_tcp_lock.unlock(saved);
        scheduler::scheduler::schedule();
    }
}

int tcp_close(tcp_pcb* pcb) noexcept {
    OutputGuard output;
    irq_lock_guard guard(g_tcp_lock);
    if (pcb->state == tcp_state::LISTEN) {
        auto* child = g_pcb_list;
        while (child) {
            auto* next = child->next;
            if (child->listener == pcb) free_locked(child);
            child = next;
        }
    }
    if (pcb->state == tcp_state::ESTABLISHED || pcb->state == tcp_state::CLOSE_WAIT) {
        pcb->state = pcb->state == tcp_state::ESTABLISHED ? tcp_state::FIN_WAIT_1 : tcp_state::LAST_ACK;
        pcb->detached = true;
        pcb->expires = time_manager::get_ticks() + 15000;
        delete[] pcb->rcv_buf;
        pcb->rcv_buf = nullptr;
        pcb->rcv_wnd = 0;
        pcb->fin_pending = true;
        send_pending_fin(pcb);
    } else
        free_locked(pcb);
    return 0;
}

int tcp_poll_events(tcp_pcb* pcb) noexcept {
    irq_lock_guard guard(g_tcp_lock);
    if (pcb->state == tcp_state::LISTEN) return pcb->accept_queue ? 1 : 0;
    int events = pcb->error ? 8 : 0;
    if (pcb->rcv_buf_head != pcb->rcv_buf_tail || pcb->state == tcp_state::CLOSE_WAIT) events |= 1;
    if ((pcb->state == tcp_state::ESTABLISHED || pcb->state == tcp_state::CLOSE_WAIT) &&
        pcb->unacked_count < MAX_DATA_SEGMENTS && pcb->snd_nxt - pcb->snd_una < pcb->snd_wnd &&
        pcb->snd_nxt - pcb->snd_una < pcb->cwnd)
        events |= 4;
    if (pcb->state == tcp_state::CLOSED) events |= 16;
    return events;
}

void tcp_tick(uint64_t now) noexcept {
    OutputGuard output;
    irq_lock_guard guard(g_tcp_lock);
    auto* pcb = g_pcb_list;
    while (pcb) {
        auto* next = pcb->next;
        bool persisting = pcb->persist_at && !pcb->snd_wnd;
        bool retry_due = !persisting && pcb->unacked_head && now >= pcb->retransmit_at;
        if ((pcb->expires && now >= pcb->expires) || (retry_due && pcb->retries == MAX_RETRIES) ||
            (persisting && now >= pcb->persist_expires)) {
            if (pcb->detached || pcb->listener)
                free_locked(pcb);
            else {
                reset_locked(pcb);
                pcb->error = -110;
                pcb->expires = 0;
            }
        } else if (persisting && now >= pcb->persist_at) {
            // An old sequence elicits the current ACK/window even when there are
            // no retained bytes (e.g. a lost window update after a full ACK).
            send_segment(pcb, TCP_ACK, nullptr, 0, true);
            pcb->persist_delay *= 2;
            if (pcb->persist_delay > MAX_PERSIST_DELAY) pcb->persist_delay = MAX_PERSIST_DELAY;
            pcb->persist_at = now + pcb->persist_delay;
        } else if (retry_due) {
            if (!pcb->retries) {
                pcb->ssthresh = (pcb->snd_nxt - pcb->snd_una) / 2;
                if (pcb->ssthresh < 2 * pcb->send_mss) pcb->ssthresh = 2 * pcb->send_mss;
            }
            pcb->cwnd = pcb->send_mss;
            pcb->congestion_credit = 0;
            invalidate_rtt(pcb);
            size_t limit = pcb->snd_wnd < pcb->send_mss ? pcb->snd_wnd : pcb->send_mss;
            auto* retry = copy_segment(pcb, pcb->unacked_head, limit);
            if (retry) enqueue_output(retry);
            ++pcb->retries; // Allocation failure also consumes a bounded attempt.
            pcb->rto *= 2;
            if (pcb->rto > MAX_RTO) pcb->rto = MAX_RTO;
            pcb->retransmit_at = now + pcb->rto;
        } else if (pcb->fin_pending) {
            // Retry deferred FIN allocation without consuming sequence space twice.
            send_pending_fin(pcb);
        }
        pcb = next;
    }
}

void tcp_init() noexcept {
    g_pcb_list = nullptr;
    g_count = 0;
    g_next_ephemeral = 49152;
    g_output_head = g_output_tail = nullptr;
    g_flushing_output = false;
}
} // namespace kernel::net
