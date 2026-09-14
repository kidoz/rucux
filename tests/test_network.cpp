// SPDX-License-Identifier: MIT
// Production protocol and socket code; replace only allocation/IO/scheduler edges.
#include "test_harness.hpp"
#include <cstdlib>
#include <cstring>
#undef htons
#undef htonl
#undef ntohs
#undef ntohl
#include <kernel/net/arp.hpp>
#include <kernel/net/ethernet.hpp>
#include <kernel/net/ipv4.hpp>
#include <kernel/net/socket.hpp>
#include <kernel/net/tcp.hpp>
#include <kernel/net/udp.hpp>
#include <kernel/time.hpp>
#include <kernel/vfs/vfs.hpp>

namespace {
size_t live_allocations;
size_t live_bytes;
struct alignas(16) Allocation {
    size_t size;
};
uint64_t ticks;
kernel::scheduler::thread current{};
kernel::vfs::vfs_node* fds[16]{};
void (*on_schedule)();
} // namespace
void* operator new(size_t size) {
    auto* header = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (!header) throw std::bad_alloc();
    header->size = size;
    ++live_allocations;
    live_bytes += size;
    return header + 1;
}
void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    auto* header = static_cast<Allocation*>(ptr) - 1;
    --live_allocations;
    live_bytes -= header->size;
    std::free(header);
}
void* operator new[](size_t size) {
    return ::operator new(size);
}
void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}
extern "C" void* kmalloc(size_t size) noexcept {
    return ::operator new(size);
}
extern "C" void kfree(void* ptr) noexcept {
    ::operator delete(ptr);
}
namespace kernel {
void kwrite(const char*) noexcept {}
void kputc(char) noexcept {}
uint64_t time_manager::get_ticks() noexcept {
    return ticks;
}
namespace console {
uintptr_t lock_output() noexcept {
    return 0;
}
void unlock_output(uintptr_t) noexcept {}
} // namespace console
namespace scheduler {
thread* scheduler::current_thread() noexcept {
    return &current;
}
void scheduler::unblock(thread* t) noexcept {
    t->state = thread_state::READY;
}
void scheduler::schedule() noexcept {
    if (!on_schedule || current.state != thread_state::BLOCKED) std::abort();
    auto fn = on_schedule;
    on_schedule = nullptr;
    fn();
    if (current.state != thread_state::READY) std::abort();
    current.state = thread_state::RUNNING;
}
} // namespace scheduler
namespace vfs {
int vfs_manager::alloc_fd(vfs_node* node) noexcept {
    for (int i = 3; i < 16; ++i)
        if (!fds[i]) {
            fds[i] = node;
            return i;
        }
    return -1;
}
vfs_node* vfs_manager::get_fd_node(int fd) noexcept {
    return fd >= 0 && fd < 16 ? fds[fd] : nullptr;
}
} // namespace vfs
} // namespace kernel
using namespace kernel::net;
namespace {
constexpr uint32_t LO = 0x0100007F;
netbuf* udp_packet(size_t length = 3, uint16_t port = 19091) {
    auto* nb = netbuf::alloc();
    auto* udp = reinterpret_cast<udp_header*>(nb->put(8 + length));
    *udp = {htons(40000), htons(port), htons(static_cast<uint16_t>(length + 8)), 0};
    std::memset(nb->data() + 8, 'x', length);
    nb->src_ip = nb->dst_ip = LO;
    return nb;
}
void add_ip(netbuf* nb) {
    auto* ip = reinterpret_cast<ipv4_header*>(nb->push(20));
    *ip = {0x45, 0, htons(static_cast<uint16_t>(nb->len())), 0, 0, 64, IPPROTO_UDP, 0, LO, LO};
    ip->checksum = checksum(ip, 20);
}
void close_fd(int fd) {
    auto* node = fds[fd];
    fds[fd] = nullptr;
    node->ops->close(node);
}
struct Address {
    uint16_t family, port;
    uint32_t ip;
    uint8_t zero[8];
};
} // namespace
TEST(netbuf_rejects_overflow_without_changing_state) {
    auto count = live_allocations;
    ASSERT(!netbuf::alloc(0));
    ASSERT(!netbuf::alloc(SIZE_MAX));
    auto* nb = netbuf::alloc();
    auto* original = nb->data();
    ASSERT(!nb->push(129));
    ASSERT(!nb->put(SIZE_MAX));
    ASSERT(!nb->pull(1));
    ASSERT(!nb->set_len(SIZE_MAX));
    ASSERT_EQ(nb->len(), 0UL);
    ASSERT_EQ(nb->data(), original);
    ASSERT(nb->put(nb->capacity()));
    ASSERT(!nb->put(1));
    ASSERT(nb->push(128));
    ASSERT(!nb->push(1));
    ASSERT(nb->pull(nb->len()));
    ASSERT(!nb->pull(1));
    netbuf::free(nb);
    ASSERT_EQ(live_allocations, count);
}
TEST(checksum_supports_unaligned_odd_payloads) {
    uint8_t bytes[] = {0, 1, 2, 3, 4, 5};
    uint8_t aligned[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(checksum(bytes + 1, 5), checksum(aligned, 5));
    ASSERT_EQ(checksum(bytes + 1, 5), 0xF9F6);
}
TEST(ipv4_checks_nested_lengths_and_fragments) {
    auto* receiver = udp_new();
    ASSERT_EQ(udp_bind(receiver, LO, htons(19091)), 0);
    auto count = live_allocations;
    for (int mode = 0; mode < 7; ++mode) {
        auto* nb = udp_packet();
        add_ip(nb);
        auto* ip = reinterpret_cast<ipv4_header*>(nb->data());
        if (mode == 0) ip->ihl_version = 0x40;
        if (mode == 1) ip->ihl_version = 0x4F;
        if (mode == 2) ip->total_length = htons(19);
        if (mode == 3) ip->total_length = htons(2000);
        if (mode == 4) ip->flags_fragment = htons(0x2000);
        if (mode == 5) ip->flags_fragment = htons(1);
        if (mode == 6) ip->ttl = 0;
        ip->checksum = 0;
        ip->checksum = checksum(ip, 20);
        ipv4_input(netif_loopback(), nb);
        ASSERT_EQ(receiver->recv_count, 0);
        ASSERT_EQ(live_allocations, count);
    }
    auto* valid = udp_packet();
    add_ip(valid);
    valid->put(20); // Ethernet padding ignored.
    ipv4_input(netif_loopback(), valid);
    char result[32];
    ASSERT_EQ(udp_recvfrom(receiver, result, sizeof(result), nullptr, nullptr), 3);
    udp_free(receiver);
}
TEST(udp_validates_length_and_optional_checksum) {
    auto* receiver = udp_new();
    ASSERT_EQ(udp_bind(receiver, LO, htons(19091)), 0);
    for (uint16_t bad : {0, 7, 500}) {
        auto* nb = udp_packet();
        reinterpret_cast<udp_header*>(nb->data())->length = htons(bad);
        udp_input(netif_loopback(), nb);
        ASSERT_EQ(receiver->recv_count, 0);
    }
    auto* nb = udp_packet();
    reinterpret_cast<udp_header*>(nb->data())->checksum = 1;
    udp_input(netif_loopback(), nb);
    ASSERT_EQ(receiver->recv_count, 0);
    nb = udp_packet();
    reinterpret_cast<udp_header*>(nb->data())->checksum = checksum_pseudo(LO, LO, IPPROTO_UDP, nb->data(), nb->len());
    udp_input(netif_loopback(), nb);
    ASSERT_EQ(receiver->recv_count, 1);
    udp_free(receiver);
}
TEST(udp_queue_is_bounded_and_close_releases_all_packets) {
    auto count = live_allocations;
    auto* receiver = udp_new();
    ASSERT_EQ(udp_bind(receiver, LO, htons(19091)), 0);
    for (int i = 0; i < 100; ++i)
        udp_input(netif_loopback(), udp_packet());
    ASSERT_EQ(receiver->recv_count, 32);
    udp_free(receiver);
    ASSERT_EQ(live_allocations, count);
}
TEST(udp_ports_bounds_and_zero_length_datagrams) {
    auto* a = udp_new();
    auto* b = udp_new();
    ASSERT_EQ(udp_bind(a, 0, htons(19091)), 0);
    ASSERT_EQ(udp_bind(b, LO, htons(19091)), -98);
    ASSERT_EQ(udp_sendto(b, "", 1473, LO, htons(19091)), -90);
    ASSERT_EQ(udp_sendto(b, "", 0, LO, htons(19091)), 0);
    ASSERT(b->local_port && b->local_port != a->local_port);
    ASSERT(udp_poll_events(a) & 1);
    uint16_t source = 0;
    ASSERT_EQ(udp_recvfrom(a, nullptr, 0, nullptr, &source), 0);
    ASSERT_EQ(source, b->local_port);
    udp_free(a);
    udp_free(b);
}
TEST(udp_wait_is_enrolled_before_input_wakeup) {
    auto* receiver = udp_new();
    ASSERT_EQ(udp_bind(receiver, LO, htons(19091)), 0);
    on_schedule = [] { udp_input(netif_loopback(), udp_packet()); };
    char data[8];
    ASSERT_EQ(udp_recvfrom(receiver, data, sizeof(data), nullptr, nullptr), 3);
    ASSERT(!on_schedule);
    udp_free(receiver);
}
TEST(tcp_reconnect_and_close_release_allocations) {
    auto count = live_allocations, bytes = live_bytes;
    auto* listener = tcp_new();
    ASSERT_EQ(tcp_bind(listener, LO, htons(19092)), 0);
    ASSERT_EQ(tcp_listen(listener, 4), 0);
    for (int cycle = 0; cycle < 100; ++cycle) {
        auto* client = tcp_new();
        ASSERT_EQ(tcp_connect(client, LO, htons(19092)), 0);
        ASSERT(tcp_poll_events(listener) & 1);
        auto* server = tcp_accept(listener);
        ASSERT(server);
        char data[8]{};
        ASSERT_EQ(tcp_send(client, "hello", 5), 5);
        ASSERT_EQ(tcp_recv(server, data, sizeof(data)), 5);
        ASSERT(std::memcmp(data, "hello", 5) == 0);
        tcp_close(client);
        ASSERT_EQ(tcp_recv(server, data, sizeof(data)), 0);
        tcp_close(server);
        ticks += 20000;
        arp_tick(ticks);
        tcp_tick(ticks);
    }
    tcp_close(listener);
    ASSERT_EQ(live_allocations, count);
    ASSERT_EQ(live_bytes, bytes);
}
TEST(tcp_receive_ring_does_not_confuse_full_and_empty) {
    auto count = live_allocations;
    auto* listener = tcp_new();
    tcp_bind(listener, LO, htons(19092));
    tcp_listen(listener, 1);
    auto* client = tcp_new();
    ASSERT_EQ(tcp_connect(client, LO, htons(19092)), 0);
    auto* server = tcp_accept(listener);
    char input[1460]{};
    size_t sent = 0;
    while (true) {
        long n = tcp_send(client, input, sizeof(input));
        if (n == -11) break;
        ASSERT(n > 0);
        sent += n;
    }
    ASSERT_EQ(sent, 32767UL);
    ASSERT(tcp_poll_events(server) & 1);
    char output[1460];
    ASSERT_EQ(tcp_recv(server, output, sizeof(output)), 1460);
    ASSERT_EQ(tcp_send(client, input, sizeof(input)), 1460);
    tcp_close(client);
    tcp_close(server);
    tcp_close(listener);
    ticks += 20000;
    arp_tick(ticks);
    tcp_tick(ticks);
    ASSERT_EQ(live_allocations, count);
}
TEST(tcp_listener_close_reclaims_unaccepted_children) {
    auto count = live_allocations;
    auto* listener = tcp_new();
    tcp_bind(listener, LO, htons(19092));
    tcp_listen(listener, 1);
    auto* client = tcp_new();
    ASSERT_EQ(tcp_connect(client, LO, htons(19092)), 0);
    ASSERT_EQ(listener->pending_count, 1);
    tcp_close(listener);
    tcp_close(client);
    ticks += 20000;
    arp_tick(ticks);
    tcp_tick(ticks);
    ASSERT_EQ(live_allocations, count);
}
TEST(tcp_connect_reset_does_not_lose_synchronous_wakeup) {
    auto* client = tcp_new();
    ASSERT_EQ(tcp_connect(client, LO, htons(19999)), -104);
    tcp_close(client);
}
TEST(tcp_invalid_offsets_are_dropped_before_subtraction) {
    auto count = live_allocations;
    for (int offset : {0, 4, 15}) {
        auto* nb = netbuf::alloc();
        auto* tcp = reinterpret_cast<tcp_header*>(nb->put(20));
        std::memset(tcp, 0, 20);
        tcp->data_offset = offset << 4;
        tcp->flags = TCP_SYN;
        nb->src_ip = nb->dst_ip = LO;
        tcp->checksum = checksum_pseudo(LO, LO, IPPROTO_TCP, tcp, 20);
        tcp_input(netif_loopback(), nb);
        ASSERT_EQ(live_allocations, count);
    }
}
TEST(tcp_backlog_and_handshake_timeouts_are_bounded) {
    static netif isolated{};
    isolated.name = "isolated";
    isolated.ip.addr = 0x0A00000A;
    isolated.ip.netmask = 0x00FFFFFF;
    isolated.mtu = 1500;
    isolated.transmit = [](netif*, netbuf* nb) noexcept { netbuf::free(nb); };
    netif_register(&isolated);
    auto count = live_allocations;
    auto* listener = tcp_new();
    ASSERT_EQ(tcp_bind(listener, isolated.ip.addr, htons(19092)), 0);
    ASSERT_EQ(tcp_listen(listener, 2), 0);
    auto baseline = live_allocations;
    for (int i = 0; i < 50; ++i) {
        auto* nb = netbuf::alloc();
        auto* tcp = reinterpret_cast<tcp_header*>(nb->put(20));
        std::memset(tcp, 0, 20);
        tcp->src_port = htons(40000 + i);
        tcp->dst_port = htons(19092);
        tcp->seq_num = htonl(123);
        tcp->data_offset = 5 << 4;
        tcp->flags = TCP_SYN;
        nb->src_ip = 0x0200000A;
        nb->dst_ip = isolated.ip.addr;
        tcp->checksum = checksum_pseudo(nb->src_ip, nb->dst_ip, IPPROTO_TCP, tcp, 20);
        tcp_input(&isolated, nb);
    }
    ASSERT_EQ(listener->pending_count, 2);
    ASSERT_EQ(tcp_poll_events(listener), 0);
    ticks += 20000;
    arp_tick(ticks);
    tcp_tick(ticks);
    ASSERT_EQ(listener->pending_count, 0);
    ASSERT_EQ(live_allocations, baseline);
    tcp_close(listener);
    ASSERT_EQ(live_allocations, count);
    auto* client = tcp_new();
    on_schedule = [] {
        ticks += 20000;
        arp_tick(ticks);
        tcp_tick(ticks);
    };
    ASSERT_EQ(tcp_connect(client, 0x0200000A, htons(19092)), -110);
    tcp_close(client);
    ASSERT_EQ(live_allocations, count);
}
TEST(socket_fd_lookup_never_falls_back_to_global_index) {
    int receiver = socket_manager::sys_socket(2, 2, 0), sender = socket_manager::sys_socket(2, 2, 0);
    Address address{2, htons(19091), LO, {}};
    ASSERT_EQ(socket_manager::sys_bind(receiver, &address, sizeof(address)), 0);
    ASSERT_EQ(socket_manager::sys_connect(sender, &address, sizeof(address)), 0);
    ASSERT_EQ(socket_manager::sys_send(0, "bad", 3, 0), -9);
    auto* node = fds[sender];
    ASSERT_EQ(node->ops->write(node, 0, 4, "good"), 4UL);
    char data[8]{};
    node = fds[receiver];
    ASSERT_EQ(node->ops->read(node, 0, 8, data), 4UL);
    ASSERT(std::memcmp(data, "good", 4) == 0);
    close_fd(sender);
    close_fd(receiver);
}
TEST(socket_fd_exhaustion_releases_protocol_state) {
    auto count = live_allocations;
    for (int i = 3; i < 16; ++i)
        ASSERT_EQ(socket_manager::sys_socket(2, 1, 0), i);
    auto full = live_allocations;
    ASSERT(socket_manager::sys_socket(2, 1, 0) < 0);
    ASSERT_EQ(live_allocations, full);
    ASSERT(socket_manager::sys_socket(2, 2, 0) < 0);
    ASSERT_EQ(live_allocations, full);
    for (int i = 3; i < 16; ++i)
        close_fd(i);
    ASSERT_EQ(live_allocations, count);
}
namespace {
uint8_t captured[64][1600];
size_t captured_len[64];
unsigned captured_count;
netif* capture_iface() {
    static netif iface{};
    if (!iface.name) {
        iface.name = "capture";
        iface.ip.addr = 0x0A00000A;
        iface.ip.netmask = 0x00FFFFFF;
        iface.mtu = 1500;
        iface.mac.bytes[0] = 2;
        iface.transmit = [](netif*, netbuf* nb) noexcept {
            if (captured_count == 64 || nb->len() > 1600) std::abort();
            captured_len[captured_count] = nb->len();
            std::memcpy(captured[captured_count++], nb->data(), nb->len());
            netbuf::free(nb);
        };
        netif_register(&iface);
    }
    return &iface;
}
void send_arp_reply(netif* iface, uint32_t sender) {
    auto* nb = netbuf::alloc();
    auto* arp = reinterpret_cast<arp_header*>(nb->put(sizeof(arp_header)));
    *arp = {};
    arp->hw_type = htons(1);
    arp->proto_type = htons(0x800);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->operation = htons(2);
    arp->sender_mac[0] = 2;
    arp->sender_mac[5] = 1;
    arp->sender_ip = sender;
    arp->target_ip = iface->ip.addr;
    std::memcpy(arp->target_mac, iface->mac.bytes, 6);
    arp_input(iface, nb);
}
tcp_pcb* established_fixture() {
    auto* iface = capture_iface();
    uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    arp_add_static(iface, 0x0200000A, mac);
    auto* pcb = tcp_new();
    if (!pcb || tcp_bind(pcb, iface->ip.addr, htons(19092))) std::abort();
    pcb->state = tcp_state::ESTABLISHED;
    pcb->remote_ip = 0x0200000A;
    pcb->remote_port = htons(40000);
    pcb->snd_nxt = pcb->snd_una = 1000;
    pcb->rcv_nxt = 4000;
    pcb->snd_wnd = 65535;
    captured_count = 0;
    return pcb;
}
void receive_ack(tcp_pcb* pcb, uint32_t ack, uint8_t flags = TCP_ACK, uint16_t window = 65535,
                 const uint8_t* options = nullptr, size_t options_length = 0, const char* payload = nullptr,
                 size_t length = 0, int32_t sequence_offset = 0) {
    auto* nb = netbuf::alloc();
    auto* tcp = reinterpret_cast<tcp_header*>(nb->put(20 + options_length + length));
    *tcp = {};
    if (options_length) std::memcpy(nb->data() + 20, options, options_length);
    if (length) std::memcpy(nb->data() + 20 + options_length, payload, length);
    tcp->src_port = pcb->remote_port;
    tcp->dst_port = pcb->local_port;
    tcp->seq_num = htonl(pcb->rcv_nxt + sequence_offset);
    tcp->ack_num = htonl(ack);
    tcp->data_offset = static_cast<uint8_t>(((20 + options_length) / 4) << 4);
    tcp->flags = flags;
    tcp->window = htons(window);
    nb->src_ip = pcb->remote_ip;
    nb->dst_ip = pcb->local_ip;
    tcp->checksum = checksum_pseudo(nb->src_ip, nb->dst_ip, IPPROTO_TCP, tcp, nb->len());
    tcp_input(capture_iface(), nb);
}
void expire_closed(tcp_pcb* pcb) {
    tcp_close(pcb);
    ticks += 20000;
    arp_tick(ticks);
    tcp_tick(ticks);
}
} // namespace
TEST(arp_queues_first_packet_retries_and_flushes_in_order) {
    auto* iface = capture_iface();
    captured_count = 0;
    auto before = live_allocations;
    for (uint8_t value : {1, 2, 3, 4}) {
        auto* nb = netbuf::alloc();
        *nb->put(1) = value;
        ethernet_output(iface, nb, 0x3200000A, 0x800);
    }
    ASSERT_EQ(captured_count, 1U);
    ASSERT_EQ(live_allocations, before + 4);
    ticks += 1000;
    arp_tick(ticks);
    ASSERT_EQ(captured_count, 2U);
    send_arp_reply(iface, 0x3200000A);
    ASSERT_EQ(captured_count, 6U);
    for (unsigned i = 2; i < 6; ++i) {
        ASSERT_EQ(captured[i][12], 8);
        ASSERT_EQ(captured[i][14], i - 1);
    }
    ASSERT_EQ(live_allocations, before);
    ticks += 4000;
    arp_tick(ticks);
    ASSERT_EQ(captured_count, 6U);
}
TEST(arp_queue_limits_and_missing_neighbor_cleanup) {
    auto* iface = capture_iface();
    captured_count = 0;
    auto before = live_allocations;
    for (int neighbor = 0; neighbor < 10; ++neighbor)
        for (int packet = 0; packet < 10; ++packet) {
            auto* nb = netbuf::alloc();
            *nb->put(1) = 1;
            ethernet_output(iface, nb, htonl(0x0A00003C + neighbor), 0x800);
        }
    ASSERT_EQ(captured_count, 8U);
    ASSERT_EQ(live_allocations, before + 32);
    ticks += 1000;
    arp_tick(ticks);
    ASSERT_EQ(captured_count, 16U);
    ticks += 1000;
    arp_tick(ticks);
    ASSERT_EQ(captured_count, 24U);
    ticks += 1000;
    arp_tick(ticks);
    ASSERT_EQ(live_allocations, before);
    ticks += 1000;
    arp_tick(ticks);
    ASSERT_EQ(captured_count, 24U);
}
TEST(arp_cache_does_not_cross_interfaces) {
    auto* iface = capture_iface();
    static netif second{};
    second.ip = iface->ip;
    second.transmit = iface->transmit;
    uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    arp_add_static(iface, 0x5000000A, mac);
    captured_count = 0;
    auto before = live_allocations;
    auto* nb = netbuf::alloc();
    *nb->put(1) = 1;
    ethernet_output(&second, nb, 0x5000000A, 0x800);
    ASSERT_EQ(captured_count, 1U);
    ASSERT_EQ(captured[0][13], 6); // ARP, not IP.
    ticks += 4000;
    arp_tick(ticks);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_retries_retained_data_and_partial_ack_suffix) {
    auto before = live_allocations, bytes = live_bytes;
    auto* pcb = established_fixture();
    char data[] = "abcdef";
    ASSERT_EQ(tcp_send(pcb, data, 6), 6);
    data[0] = 'z';
    ASSERT_EQ(pcb->unacked_count, 1U);
    ASSERT_EQ(captured_count, 1U);
    ticks = pcb->retransmit_at - 1;
    tcp_tick(ticks);
    ASSERT_EQ(captured_count, 1U);
    ++ticks;
    tcp_tick(ticks);
    ASSERT_EQ(captured_count, 2U);
    ASSERT(std::memcmp(captured[1] + 54, "abcdef", 6) == 0);
    ASSERT_EQ(pcb->snd_nxt, 1006U);
    receive_ack(pcb, 1003);
    ASSERT_EQ(pcb->unacked_count, 1U);
    ticks = pcb->retransmit_at;
    tcp_tick(ticks);
    ASSERT_EQ(captured_count, 3U);
    auto* header = reinterpret_cast<tcp_header*>(captured[2] + 34);
    ASSERT_EQ(ntohl(header->seq_num), 1003U);
    ASSERT_EQ(captured_len[2], 57UL);
    ASSERT(std::memcmp(captured[2] + 54, "def", 3) == 0);
    ASSERT_EQ(checksum_pseudo(pcb->local_ip, pcb->remote_ip, IPPROTO_TCP, header, 23), 0);
    receive_ack(pcb, 1006);
    ASSERT_EQ(pcb->unacked_count, 0U);
    ticks += 10000;
    tcp_tick(ticks);
    ASSERT_EQ(captured_count, 3U);
    expire_closed(pcb);
    ASSERT_EQ(live_allocations, before);
    ASSERT_EQ(live_bytes, bytes);
}
TEST(tcp_unacknowledged_queue_applies_backpressure) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    for (unsigned i = 0; i < 8; ++i)
        ASSERT_EQ(tcp_send(pcb, "x", 1), 1);
    ASSERT_EQ(tcp_send(pcb, "x", 1), -11);
    ASSERT(!(tcp_poll_events(pcb) & 4));
    receive_ack(pcb, 1004);
    ASSERT_EQ(pcb->unacked_count, 4U);
    ASSERT(tcp_poll_events(pcb) & 4);
    ASSERT_EQ(tcp_send(pcb, "x", 1), 1);
    tcp_close(pcb);
    ASSERT_EQ(pcb->unacked_count, 6U); // Data plus FIN retained after close.
    ticks += 20000;
    tcp_tick(ticks);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_retry_exhaustion_wakes_reader_and_reclaims_packets) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    ASSERT_EQ(tcp_send(pcb, "lost", 4), 4);
    pcb->wait_recv = &current;
    current.state = kernel::scheduler::thread_state::BLOCKED;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        ticks = pcb->retransmit_at;
        tcp_tick(ticks);
    }
    ASSERT_EQ(captured_count, 5U);
    ASSERT_EQ(pcb->retries, 4U);
    // Duplicate ACKs do not indefinitely postpone timeout.
    auto deadline = pcb->retransmit_at;
    receive_ack(pcb, 1000);
    ASSERT_EQ(pcb->retransmit_at, deadline);
    ticks = pcb->retransmit_at;
    tcp_tick(ticks);
    ASSERT_EQ(pcb->state, tcp_state::CLOSED);
    ASSERT_EQ(pcb->error, -110);
    ASSERT_EQ(pcb->unacked_count, 0U);
    ASSERT_EQ(current.state, kernel::scheduler::thread_state::READY);
    tcp_close(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_active_connect_retries_syn_before_wakeup) {
    auto before = live_allocations;
    auto* iface = capture_iface();
    uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    arp_add_static(iface, 0x0200000A, mac);
    static tcp_pcb* connecting;
    connecting = tcp_new();
    captured_count = 0;
    on_schedule = [] {
        ticks = connecting->retransmit_at;
        tcp_tick(ticks);
        receive_ack(connecting, connecting->snd_nxt, TCP_SYN | TCP_ACK);
    };
    ASSERT_EQ(tcp_connect(connecting, 0x0200000A, htons(19092)), 0);
    ASSERT_EQ(captured_count, 3U);
    auto* first = reinterpret_cast<tcp_header*>(captured[0] + 34);
    auto* second = reinterpret_cast<tcp_header*>(captured[1] + 34);
    ASSERT_EQ(first->flags, TCP_SYN);
    ASSERT_EQ(second->flags, TCP_SYN);
    ASSERT_EQ(first->seq_num, second->seq_num);
    ASSERT_EQ(connecting->unacked_count, 0U);
    expire_closed(connecting);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_simultaneous_retries_do_not_recurse_through_loopback) {
    auto before = live_allocations;
    auto* listener = tcp_new();
    tcp_bind(listener, LO, htons(19092));
    tcp_listen(listener, 1);
    tcp_pcb* clients[32];
    tcp_pcb* servers[32];
    for (unsigned i = 0; i < 32; ++i) {
        clients[i] = tcp_new();
        ASSERT_EQ(tcp_connect(clients[i], LO, htons(19092)), 0);
        servers[i] = tcp_accept(listener);
    }
    auto original = netif_loopback()->transmit;
    netif_loopback()->transmit = [](netif*, netbuf* buf) noexcept { netbuf::free(buf); };
    for (auto* client : clients)
        ASSERT_EQ(tcp_send(client, "x", 1), 1);
    static unsigned depth, maximum;
    depth = maximum = 0;
    netif_loopback()->transmit = [](netif* iface, netbuf* buf) noexcept {
        ++depth;
        if (depth > maximum) maximum = depth;
        netif_input(iface, buf);
        --depth;
    };
    ticks = clients[0]->retransmit_at;
    tcp_tick(ticks);
    netif_loopback()->transmit = original;
    ASSERT_EQ(maximum, 1U);
    for (unsigned i = 0; i < 32; ++i) {
        ASSERT_EQ(clients[i]->unacked_count, 0U);
        char data;
        ASSERT_EQ(tcp_recv(servers[i], &data, 1), 1);
        ASSERT_EQ(data, 'x');
        tcp_close(clients[i]);
        tcp_close(servers[i]);
    }
    tcp_close(listener);
    ticks += 20000;
    tcp_tick(ticks);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_reset_frees_retransmission_state) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    ASSERT_EQ(tcp_send(pcb, "reset", 5), 5);
    receive_ack(pcb, 1000, TCP_RST);
    ASSERT_EQ(pcb->unacked_count, 0U);
    ASSERT_EQ(pcb->error, -104);
    tcp_close(pcb);
    ASSERT_EQ(live_allocations, before);
}

TEST(tcp_congestion_limits_flight_and_recovers_after_timeout) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    char data[1460]{};
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), 536);
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), -11);
    ASSERT(!(tcp_poll_events(pcb) & 4));
    receive_ack(pcb, 1536);
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), 536);
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), 536);
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), -11);
    ticks = pcb->retransmit_at;
    tcp_tick(ticks);
    ASSERT_EQ(captured_count, 4U);
    ASSERT_EQ(captured_len[3], 54UL + 536);
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), -11);
    receive_ack(pcb, pcb->snd_nxt);
    ASSERT_EQ(pcb->cwnd, 1072U);
    // At ssthresh, one ACK of half the window does not grow cwnd.
    ASSERT_EQ(tcp_send(pcb, data, 536), 536);
    ASSERT_EQ(tcp_send(pcb, data, 536), 536);
    uint32_t first_ack = pcb->snd_nxt - 536;
    receive_ack(pcb, first_ack);
    ASSERT_EQ(pcb->cwnd, 1072U);
    receive_ack(pcb, first_ack + 536);
    ASSERT_EQ(pcb->cwnd, 1608U);
    ticks += pcb->rto;
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), 536);
    ASSERT_EQ(tcp_send(pcb, data, 1), -11); // Idle restart also limits bursts.
    expire_closed(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_rtt_estimator_ignores_ambiguous_ack_and_backoffs) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    tcp_send(pcb, "a", 1);
    ticks += 600;
    receive_ack(pcb, pcb->snd_nxt);
    ASSERT_EQ(pcb->rto, 1800ULL);
    tcp_send(pcb, "b", 1);
    ticks += 1000;
    receive_ack(pcb, pcb->snd_nxt);
    ASSERT_EQ(pcb->rto, 1950ULL);
    tcp_send(pcb, "cd", 2);
    ticks = pcb->retransmit_at;
    tcp_tick(ticks);
    ASSERT_EQ(pcb->rto, 3900ULL);
    ticks += 10;
    receive_ack(pcb, pcb->snd_nxt - 1); // Partial ACK is ambiguous too.
    ASSERT_EQ(pcb->rto, 3900ULL);
    receive_ack(pcb, pcb->snd_nxt);
    ASSERT_EQ(pcb->rto, 3900ULL);
    tcp_send(pcb, "e", 1);
    ticks += 100;
    receive_ack(pcb, pcb->snd_nxt);
    ASSERT(pcb->rto < 3900 && pcb->rto >= 1000);
    expire_closed(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_mss_options_are_validated_and_negotiated_in_both_directions) {
    auto before = live_allocations;
    // Exercise absent, tiny, typical and oversized peer MSS values.
    for (uint32_t mss : {0U, 1U, 256U, 1200U, 65535U}) {
        auto* listener = established_fixture();
        listener->state = tcp_state::CLOSED;
        ASSERT_EQ(tcp_listen(listener, 1), 0);
        uint8_t options[] = {1, 2, 4, static_cast<uint8_t>(mss >> 8), static_cast<uint8_t>(mss), 0, 0, 0};
        receive_ack(listener, 0, TCP_SYN, 65535, options, mss ? sizeof(options) : 0);
        ASSERT_EQ(listener->pending_count, 1);
        ASSERT_EQ(captured_count, 1U);
        auto* syn = reinterpret_cast<tcp_header*>(captured[0] + 34);
        ASSERT_EQ(syn->data_offset, 6 << 4);
        ASSERT(std::memcmp(captured[0] + 54, "\x02\x04\x05\xb4", 4) == 0);
        ++listener->rcv_nxt; // Build the peer's final handshake ACK.
        receive_ack(listener, ntohl(syn->seq_num) + 1);
        auto* pcb = tcp_accept(listener);
        uint32_t expected = !mss ? 536 : (mss > 1460 ? 1460 : mss);
        char data[1460]{};
        ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), static_cast<long>(expected));
        ASSERT_EQ(tcp_send(pcb, data, 1), -11);
        tcp_free(pcb);
        tcp_close(listener);
    }
    auto* pcb = established_fixture();
    pcb->state = tcp_state::SYN_SENT;
    const uint8_t options[] = {2, 4, 1, 44};
    receive_ack(pcb, pcb->snd_nxt, TCP_SYN | TCP_ACK, 65535, options, sizeof(options));
    ASSERT_EQ(pcb->state, tcp_state::ESTABLISHED);
    char data[1000]{};
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), 300);
    tcp_free(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_malformed_options_do_not_allocate_children) {
    auto* listener = established_fixture();
    listener->state = tcp_state::CLOSED;
    tcp_listen(listener, 1);
    auto before = live_allocations;
    const uint8_t malformed[][8] = {
        {2, 0}, {2, 1}, {2, 3, 1}, {2, 4, 0, 0}, {3, 9}, {1, 1, 1, 1, 1, 1, 1, 3}, {2, 4, 1, 0, 2, 4, 1, 0}};
    for (const auto& options : malformed) {
        receive_ack(listener, 0, TCP_SYN, 65535, options, sizeof(options));
        ASSERT_EQ(listener->pending_count, 0);
        ASSERT_EQ(captured_count, 0U);
        ASSERT_EQ(live_allocations, before);
    }
    tcp_close(listener);
}
TEST(tcp_zero_window_recovers_lost_update_without_retained_data) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    tcp_send(pcb, "x", 1);
    receive_ack(pcb, pcb->snd_nxt, TCP_ACK, 0);
    ASSERT_EQ(pcb->unacked_count, 0U);
    auto stalled_allocations = live_allocations, stalled_bytes = live_bytes;
    ASSERT(!(tcp_poll_events(pcb) & 4));
    ASSERT_EQ(tcp_send(pcb, "x", 1), -11);
    // A reader can stay stalled for minutes provided it answers the probes.
    for (unsigned i = 0; i < 100; ++i) {
        captured_count = 0;
        ticks = pcb->persist_at;
        tcp_tick(ticks);
        ASSERT_EQ(captured_count, 1U);
        ASSERT_EQ(captured_len[0], 54UL);
        auto* probe = reinterpret_cast<tcp_header*>(captured[0] + 34);
        ASSERT_EQ(ntohl(probe->seq_num), pcb->snd_una - 1);
        ASSERT_EQ(probe->flags, TCP_ACK);
        ASSERT_EQ(pcb->snd_nxt, 1001U);
        ASSERT_EQ(pcb->retries, 0U);
        receive_ack(pcb, pcb->snd_nxt, TCP_ACK, 0);
        ASSERT_EQ(live_allocations, stalled_allocations);
        ASSERT_EQ(live_bytes, stalled_bytes);
    }
    receive_ack(pcb, pcb->snd_nxt, TCP_ACK, 128);
    ASSERT(tcp_poll_events(pcb) & 4);
    char data[256]{};
    ASSERT_EQ(tcp_send(pcb, data, sizeof(data)), 128);
    expire_closed(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_zero_window_retains_data_and_respects_reopened_window) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    ASSERT_EQ(tcp_send(pcb, "abcdef", 6), 6);
    receive_ack(pcb, pcb->snd_una, TCP_ACK, 0);
    ticks = pcb->persist_at;
    tcp_tick(ticks);
    ASSERT_EQ(pcb->unacked_count, 1U);
    ASSERT_EQ(pcb->retries, 0U);
    receive_ack(pcb, pcb->snd_una, TCP_ACK, 2);
    ASSERT_EQ(captured_count, 3U);
    ASSERT_EQ(captured_len[2], 56UL);
    ASSERT(std::memcmp(captured[2] + 54, "ab", 2) == 0);
    ASSERT_EQ(pcb->rto, 1000ULL);
    receive_ack(pcb, 1002, TCP_ACK, 2);
    ticks = pcb->retransmit_at;
    tcp_tick(ticks);
    ASSERT_EQ(captured_len[3], 56UL);
    ASSERT(std::memcmp(captured[3] + 54, "cd", 2) == 0);
    receive_ack(pcb, 1004, TCP_ACK, 0);
    tcp_close(pcb);
    ASSERT(pcb->fin_pending);
    ASSERT_EQ(pcb->snd_nxt, 1006U); // FIN waits for peer window space.
    receive_ack(pcb, 1006, TCP_ACK, 1);
    ASSERT(!pcb->fin_pending);
    ASSERT_EQ(pcb->snd_nxt, 1007U);
    receive_ack(pcb, 1007);
    ASSERT_EQ(pcb->state, tcp_state::FIN_WAIT_2);
    ticks += 20000;
    tcp_tick(ticks);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_zero_window_silent_peer_times_out_and_wakes_waiter) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    receive_ack(pcb, pcb->snd_nxt, TCP_ACK, 0);
    pcb->wait_send = &current;
    current.state = kernel::scheduler::thread_state::BLOCKED;
    ticks = pcb->persist_expires;
    tcp_tick(ticks);
    ASSERT_EQ(current.state, kernel::scheduler::thread_state::READY);
    ASSERT_EQ(pcb->state, tcp_state::CLOSED);
    ASSERT_EQ(pcb->error, -110);
    tcp_close(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_out_of_order_packets_are_acked_and_retransmission_recovers) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    receive_ack(pcb, 1000, TCP_ACK, 65535, nullptr, 0, "def", 3, 3);
    ASSERT(!(tcp_poll_events(pcb) & 1));
    ASSERT_EQ(pcb->rcv_nxt, 4000U);
    receive_ack(pcb, 1000, TCP_ACK, 65535, nullptr, 0, "abc", 3);
    // The peer resends the dropped suffix after the cumulative ACK.
    receive_ack(pcb, 1000, TCP_ACK, 65535, nullptr, 0, "def", 3);
    receive_ack(pcb, 1000, TCP_ACK, 65535, nullptr, 0, "abc", 3, -6);
    ASSERT_EQ(pcb->rcv_nxt, 4006U);
    char data[8]{};
    ASSERT_EQ(tcp_recv(pcb, data, sizeof(data)), 6);
    ASSERT(std::memcmp(data, "abcdef", 6) == 0);
    expire_closed(pcb);
    ASSERT_EQ(live_allocations, before);
}
TEST(tcp_sequence_wrap_preserves_partial_ack_and_mss_suffix) {
    auto before = live_allocations;
    auto* pcb = established_fixture();
    pcb->snd_nxt = pcb->snd_una = 0xFFFFFFFEU;
    ASSERT_EQ(tcp_send(pcb, "wrap", 4), 4);
    receive_ack(pcb, 0);
    ASSERT_EQ(pcb->snd_una, 0U);
    ticks = pcb->retransmit_at;
    tcp_tick(ticks);
    ASSERT_EQ(captured_len[1], 56UL);
    ASSERT(std::memcmp(captured[1] + 54, "ap", 2) == 0);
    receive_ack(pcb, 2);
    ASSERT_EQ(pcb->unacked_count, 0U);
    expire_closed(pcb);
    ASSERT_EQ(live_allocations, before);
}

int main() {
    net_init();
    socket_manager::init();
    return rucux_test::run_all_tests("Network protocols and sockets");
}
