// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/net/ipv4.hpp>
#include <kernel/net/netbuf.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::net {

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset; // upper 4 bits = header length in 32-bit words
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

// TCP flags
static constexpr uint8_t TCP_FIN = 0x01;
static constexpr uint8_t TCP_SYN = 0x02;
static constexpr uint8_t TCP_RST = 0x04;
static constexpr uint8_t TCP_PSH = 0x08;
static constexpr uint8_t TCP_ACK = 0x10;

static constexpr size_t TCP_HEADER_LEN = 20;

enum class tcp_state : uint8_t {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT
};

// TCP Protocol Control Block — one per connection
struct tcp_pcb {
    tcp_state state;
    bool detached; // Application closed; retain only bounded teardown state.
    int error;
    uint64_t expires;

    // Local and remote endpoints
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    // Sequence numbers
    uint32_t snd_una; // Oldest unacknowledged
    uint32_t snd_nxt; // Next to send
    uint32_t snd_wnd; // Send window
    uint32_t rcv_nxt; // Next expected receive seq
    uint32_t rcv_wnd; // Receive window
    uint32_t iss;     // Initial send sequence number

    // Immutable sequence ranges retained until cumulatively acknowledged.
    netbuf* unacked_head;
    netbuf* unacked_tail;
    unsigned unacked_count;
    unsigned retries;
    uint64_t retransmit_at;

    // Receive buffer (data received, waiting for app to read)
    uint8_t* rcv_buf;
    size_t rcv_buf_size;
    size_t rcv_buf_head;
    size_t rcv_buf_tail;

    // Backlog (for LISTEN sockets)
    tcp_pcb* accept_queue;
    tcp_pcb* accept_tail;
    tcp_pcb* next_accept; // Link in accept queue
    int backlog;
    int pending_count;

    // Blocking threads
    scheduler::thread* wait_connect;
    scheduler::thread* wait_accept;
    scheduler::thread* wait_recv;
    scheduler::thread* wait_send;

    irq_spinlock lock;

    // Link in global PCB list
    tcp_pcb* next;

    // Parent listener (for SYN_RECEIVED sockets)
    tcp_pcb* listener;
};

// ─── TCP API ───────────────────────────────────────────────────────────────

void tcp_init() noexcept;
void tcp_tick(uint64_t now) noexcept;
void tcp_input(netif* iface, netbuf* buf) noexcept;

tcp_pcb* tcp_new() noexcept;
void tcp_free(tcp_pcb* pcb) noexcept;

int tcp_bind(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept;
int tcp_listen(tcp_pcb* pcb, int backlog) noexcept;
tcp_pcb* tcp_accept(tcp_pcb* pcb) noexcept;
int tcp_connect(tcp_pcb* pcb, uint32_t addr, uint16_t port) noexcept;
long tcp_send(tcp_pcb* pcb, const void* data, size_t len) noexcept;
long tcp_recv(tcp_pcb* pcb, void* data, size_t len) noexcept;
int tcp_close(tcp_pcb* pcb) noexcept;
int tcp_poll_events(tcp_pcb* pcb) noexcept; // Returns POLLIN|POLLOUT mask

} // namespace kernel::net
