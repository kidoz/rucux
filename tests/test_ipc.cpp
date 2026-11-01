// SPDX-License-Identifier: MIT
// Production IPC, signal teardown, credentials and syscall marshalling. Hosted
// threads model scheduling; user-copy edges accept the harness's real buffers.
#include "test_harness.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <kernel/ipc/ipc.hpp>
#include <kernel/memory/user_access.hpp>
#include <kernel/process/signal.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/syscall.hpp>
#include <kernel/vfs/vfs.hpp>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <uapi/kernel/syscalls.h>
#include <vector>

namespace {
using namespace kernel;
using namespace kernel::ipc;
using scheduler::thread;
using scheduler::thread_state;
std::array<thread, 70> peers;
std::mutex registry_lock, schedule_lock;
std::condition_variable scheduled_cv;
std::vector<thread*> registry;
std::array<std::atomic<unsigned>, 128> schedules{}, stops{}, closes{};
thread_local thread* running;
thread_local std::function<void(thread*)> after_pin;

void clean_links(const thread& owner) {
    ASSERT(owner.ipc_peer == nullptr);
    ASSERT(owner.ipc_caller == nullptr);
    ASSERT(owner.send_queue_head == nullptr);
    ASSERT(owner.send_queue_tail == nullptr);
    ASSERT(owner.send_queue_next == nullptr);
    ASSERT(owner.recv_buffer == nullptr);
    ASSERT_EQ(owner.ipc_queue_count, 0UL);
    ASSERT_EQ(owner.pins.load(), 0U);
    ASSERT(!owner.interruptible_wait);
}
void reset() {
    std::lock_guard guard(registry_lock);
    registry.clear();
    for (unsigned i = 0; i < peers.size(); ++i) {
        ASSERT_EQ(peers[i].pins.load(), 0U);
        new (&peers[i]) thread{};
        peers[i].tid = i + 1;
        peers[i].user_entry = reinterpret_cast<void*>(1);
        peers[i].creds = process::root_credentials(i + 1, i + 1);
        peers[i].state.store(thread_state::RUNNING);
        registry.push_back(&peers[i]);
    }
    for (unsigned i = 0; i < schedules.size(); ++i) {
        schedules[i].store(0);
        stops[i].store(0);
        closes[i].store(0);
    }
    running = &peers[0];
    after_pin = nullptr;
}
long dispatch(long number, long a1, long a2, long, long, long, long) {
    switch (number) {
    case SYS_IPC_SEND:
        return ipc_manager::send_sync(static_cast<uint32_t>(a1), *reinterpret_cast<message*>(a2));
    case SYS_IPC_RECV:
        return ipc_manager::receive_sync(*reinterpret_cast<message*>(a1));
    case SYS_IPC_CALL:
        return sys_ipc_call(static_cast<uint32_t>(a1), reinterpret_cast<fast_msg*>(a2));
    case SYS_IPC_WAIT:
        return sys_ipc_wait(reinterpret_cast<fast_msg*>(a1));
    case SYS_IPC_REPLY:
        return sys_ipc_reply(reinterpret_cast<fast_msg*>(a1));
    default:
        return -38;
    }
}
long invoke(long number, long a1 = 0, long a2 = 0) {
    return checked_syscall(dispatch, number, a1, a2, 0, 0, 0, 0);
}
long call(thread& target, fast_msg& msg) {
    return invoke(SYS_IPC_CALL, target.tid, reinterpret_cast<long>(&msg));
}
long wait(fast_msg& msg) {
    return invoke(SYS_IPC_WAIT, reinterpret_cast<long>(&msg));
}
long reply(fast_msg& msg) {
    return invoke(SYS_IPC_REPLY, reinterpret_cast<long>(&msg));
}
void wait_scheduled(thread& target, unsigned count = 1) {
    std::unique_lock guard(schedule_lock);
    ASSERT(
        scheduled_cv.wait_for(guard, std::chrono::seconds(3), [&] { return schedules[target.tid].load() >= count; }));
}
std::thread launch(thread& owner, const std::function<void()>& body) {
    return std::thread([&owner, body] {
        running = &owner;
        body();
    });
}
void exit_peer(thread& owner) {
    detach_thread(&owner);
    owner.state.store(thread_state::TERMINATED);
}
void cancel(thread& owner) {
    ASSERT_EQ(process::signal_manager::post_signal(&owner, 15), 0);
}
void roundtrip(thread& client, thread& server) {
    fast_msg data{12, 34, 56, 78};
    std::atomic<long> result{-999};
    auto worker = launch(client, [&] { result.store(call(server, data)); });
    wait_scheduled(client);
    running = &server;
    fast_msg request{};
    ASSERT_EQ(wait(request), 0L);
    ASSERT_EQ(request.d0, 34ULL);
    request.d0 = 99;
    ASSERT_EQ(reply(request), 0L);
    worker.join();
    ASSERT_EQ(result.load(), 0L);
    ASSERT_EQ(data.d0, 99ULL);
    clean_links(client);
    clean_links(server);
}
} // namespace
namespace kernel::scheduler {
void scheduler::publish_termination(thread* owner, int status) noexcept {
    owner->exit_code = status;
    owner->exited = true;
    owner->state.store(thread_state::TERMINATED, kernel::release);
}
thread* scheduler::current_thread() noexcept {
    return running;
}
thread* scheduler::get_thread_by_tid(uint32_t tid) noexcept {
    std::lock_guard guard(registry_lock);
    for (auto* owner : registry)
        if (owner->tid == tid) return owner;
    return nullptr;
}
thread* scheduler::pin_thread(uint32_t tid) noexcept {
    thread* result = nullptr;
    {
        std::lock_guard guard(registry_lock);
        for (auto* owner : registry) {
            if (owner->tid == tid && !owner->ipc_closed.load() && owner->state.load() != thread_state::TERMINATED) {
                owner->pin();
                result = owner;
                break;
            }
        }
    }
    if (result && after_pin) {
        auto callback = std::move(after_pin);
        after_pin = nullptr;
        callback(result);
    }
    return result;
}
void scheduler::add_thread(thread* owner) noexcept {
    {
        std::lock_guard guard(schedule_lock);
        owner->state.store(thread_state::READY);
    }
    scheduled_cv.notify_all();
}
void scheduler::unblock(thread* owner) noexcept {
    irq_lock_guard guard(owner->wait_lock);
    if (owner->state.load() == thread_state::TERMINATED) return;
    if (owner->interruptible_wait && owner->state.load() != thread_state::BLOCKED) return;
    add_thread(owner);
}
void scheduler::schedule() noexcept {
    std::unique_lock guard(schedule_lock);
    schedules[running->tid].fetch_add(1);
    scheduled_cv.notify_all();
    if (!scheduled_cv.wait_for(guard, std::chrono::seconds(3),
                               [&] { return running->state.load() != thread_state::BLOCKED; }))
        std::abort();
    running->state.store(thread_state::RUNNING);
}
void scheduler::finish_termination() noexcept {
    stops[running->tid].fetch_add(1);
}
int scheduler::signal_thread(int pid, int sig) noexcept {
    auto* target = pin_thread(pid == 0 ? running->tid : static_cast<uint32_t>(pid));
    if (!target) return -3;
    const int result = process::signal_manager::post_signal(target, sig);
    target->unpin();
    return result;
}
} // namespace kernel::scheduler
namespace kernel::vfs {
void vfs_manager::close_all(scheduler::thread* owner) noexcept {
    if (owner->ipc_peer || owner->ipc_caller || owner->send_queue_head || owner->recv_buffer) std::abort();
    closes[owner->tid].fetch_add(1);
}
} // namespace kernel::vfs
namespace kernel::memory {
bool user_accessible(const void* ptr, size_t size, bool) noexcept {
    return ptr || !size;
}
bool copy_from_user(void* destination, const void* source, size_t size) noexcept {
    if (size && (!destination || !source)) return false;
    if (size) std::memcpy(destination, source, size);
    return true;
}
bool copy_to_user(void* destination, const void* source, size_t size) noexcept {
    return copy_from_user(destination, source, size);
}
long copy_user_string(char* destination, const char* source, size_t capacity) noexcept {
    if (!source) return -14;
    const size_t size = std::strlen(source);
    if (size >= capacity) return -36;
    std::memcpy(destination, source, size + 1);
    return static_cast<long>(size);
}
} // namespace kernel::memory
TEST(async_message_wakes_and_is_copied_by_a_blocked_legacy_receiver) {
    reset();
    message output{};
    std::atomic<long> result{-999};
    auto worker = launch(peers[1], [&] { result.store(invoke(SYS_IPC_RECV, reinterpret_cast<long>(&output))); });
    wait_scheduled(peers[1]);
    message data{999, 42, {1, 2, 3, 4}};
    ASSERT_EQ(ipc_manager::send_async(peers[1].tid, data), 0);
    worker.join();
    ASSERT_EQ(result.load(), 0L);
    ASSERT_EQ(output.type, 42U);
    ASSERT_EQ(output.sender, peers[0].tid);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(legacy_senders_preserve_fifo_and_authenticated_identity) {
    reset();
    std::array<std::thread, 3> workers;
    std::array<message, 3> data{};
    std::array<std::atomic<long>, 3> results;
    for (unsigned i = 0; i < workers.size(); ++i) {
        data[i] = {999, i + 10, {i, 0, 0, 0}};
        workers[i] = launch(peers[i + 1], [&, i] {
            results[i].store(invoke(SYS_IPC_SEND, peers[0].tid, reinterpret_cast<long>(&data[i])));
        });
        wait_scheduled(peers[i + 1]);
    }
    for (unsigned i = 0; i < workers.size(); ++i) {
        message output{};
        ASSERT_EQ(invoke(SYS_IPC_RECV, reinterpret_cast<long>(&output)), 0L);
        ASSERT_EQ(output.type, i + 10);
        ASSERT_EQ(output.sender, peers[i + 1].tid);
    }
    for (unsigned i = 0; i < workers.size(); ++i) {
        workers[i].join();
        ASSERT_EQ(results[i].load(), 0L);
        clean_links(peers[i + 1]);
    }
    clean_links(peers[0]);
}
TEST(async_ring_reports_capacity_and_preserves_fifo) {
    reset();
    for (unsigned i = 0; i < thread::ASYNC_QUEUE_SIZE - 1; ++i) {
        message data{999, i, {}};
        ASSERT_EQ(ipc_manager::send_async(peers[0].tid, data), 0);
    }
    message extra{};
    ASSERT_EQ(ipc_manager::send_async(peers[0].tid, extra), -11);
    for (unsigned i = 0; i < thread::ASYNC_QUEUE_SIZE - 1; ++i) {
        message output{};
        ASSERT_EQ(invoke(SYS_IPC_RECV, reinterpret_cast<long>(&output)), 0L);
        ASSERT_EQ(output.type, i);
        ASSERT_EQ(output.sender, peers[0].tid);
    }
    clean_links(peers[0]);
}
TEST(fixed_call_roundtrip_and_multiple_callers_do_not_overwrite_active_request) {
    reset();
    std::array<std::thread, 3> workers;
    std::array<fast_msg, 3> data{};
    std::array<std::atomic<long>, 3> results;
    for (unsigned i = 0; i < workers.size(); ++i) {
        data[i] = {10 + i, i, 100, 200};
        workers[i] = launch(peers[i + 1], [&, i] { results[i].store(call(peers[0], data[i])); });
        wait_scheduled(peers[i + 1]);
    }
    for (unsigned i = 0; i < workers.size(); ++i) {
        fast_msg request{}, untouched{99, 98, 97, 96};
        ASSERT_EQ(wait(request), 0L);
        ASSERT_EQ(request.d0, static_cast<uint64_t>(i));
        ASSERT_EQ(wait(untouched), -16L);
        ASSERT_EQ(untouched.type, 99ULL);
        request.d0 = i + 50;
        ASSERT_EQ(reply(request), 0L);
    }
    for (unsigned i = 0; i < workers.size(); ++i) {
        workers[i].join();
        ASSERT_EQ(results[i].load(), 0L);
        ASSERT_EQ(data[i].d0, i + 50);
        clean_links(peers[i + 1]);
    }
    clean_links(peers[0]);
}
TEST(async_traffic_does_not_wake_a_fixed_receiver_or_complete_a_call) {
    reset();
    std::atomic<long> server_result{-999};
    auto server = launch(peers[1], [&] {
        fast_msg request{};
        server_result.store(wait(request));
        ASSERT_EQ(reply(request), 0L);
    });
    wait_scheduled(peers[1]);
    message async{0, 41, {}};
    ASSERT_EQ(ipc_manager::send_async(peers[1].tid, async), 0);
    ASSERT(peers[1].state.load() == thread_state::BLOCKED);
    fast_msg request{1, 2, 3, 4};
    ASSERT_EQ(call(peers[1], request), 0L);
    server.join();
    ASSERT_EQ(server_result.load(), 0L);
    running = &peers[1];
    message received{};
    ASSERT_EQ(invoke(SYS_IPC_RECV, reinterpret_cast<long>(&received)), 0L);
    ASSERT_EQ(received.type, 41U);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(async_traffic_cannot_forge_a_reply_for_a_waiting_caller) {
    reset();
    fast_msg data{1, 2, 3, 4};
    std::atomic<long> result{-999};
    auto caller = launch(peers[1], [&] { result.store(call(peers[0], data)); });
    wait_scheduled(peers[1]);
    fast_msg request{};
    ASSERT_EQ(wait(request), 0L);
    message async{0, 42, {}};
    ASSERT_EQ(ipc_manager::send_async(peers[1].tid, async), 0);
    ASSERT(peers[1].state.load() == thread_state::BLOCKED);
    request.d0 = 100;
    ASSERT_EQ(reply(request), 0L);
    caller.join();
    ASSERT_EQ(result.load(), 0L);
    ASSERT_EQ(data.d0, 100ULL);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(cancelled_queued_sender_detaches_and_closes_after_kernel_unwind) {
    reset();
    message data{0, 123, {}};
    std::atomic<long> result{-999};
    auto sender =
        launch(peers[1], [&] { result.store(invoke(SYS_IPC_SEND, peers[0].tid, reinterpret_cast<long>(&data))); });
    wait_scheduled(peers[1]);
    cancel(peers[1]);
    sender.join();
    ASSERT_EQ(result.load(), -4L);
    ASSERT_EQ(stops[peers[1].tid].load(), 1U);
    ASSERT_EQ(closes[peers[1].tid].load(), 1U);
    clean_links(peers[0]);
    clean_links(peers[1]);
    roundtrip(peers[2], peers[0]);
}
TEST(cancelled_accepted_caller_leaves_tombstone_until_reply_acknowledges_it) {
    reset();
    fast_msg data{1, 2, 3, 4}, original = data;
    std::atomic<long> result{-999};
    auto caller = launch(peers[1], [&] { result.store(call(peers[0], data)); });
    wait_scheduled(peers[1]);
    fast_msg request{};
    ASSERT_EQ(wait(request), 0L);
    cancel(peers[1]);
    caller.join();
    ASSERT_EQ(result.load(), -4L);
    ASSERT(std::memcmp(&data, &original, sizeof(data)) == 0);
    ASSERT_EQ(wait(request), -125L);
    ASSERT_EQ(reply(request), -125L);
    ASSERT_EQ(reply(request), -2L);
    clean_links(peers[0]);
    clean_links(peers[1]);
    roundtrip(peers[2], peers[0]);
}
TEST(peer_exit_fails_queued_call_and_preserves_caller_output) {
    reset();
    fast_msg data{1, 2, 3, 4}, original = data;
    std::atomic<long> result{-999};
    auto caller = launch(peers[1], [&] { result.store(call(peers[0], data)); });
    wait_scheduled(peers[1]);
    exit_peer(peers[0]);
    caller.join();
    ASSERT_EQ(result.load(), -32L);
    ASSERT(std::memcmp(&data, &original, sizeof(data)) == 0);
    clean_links(peers[0]);
    clean_links(peers[1]);
    roundtrip(peers[2], peers[3]);
}
TEST(peer_exit_fails_accepted_call_and_all_queued_senders) {
    reset();
    fast_msg data{1, 2, 3, 4};
    message legacy{0, 42, {}};
    std::atomic<long> call_result{-999}, send_result{-999};
    auto caller = launch(peers[1], [&] { call_result.store(call(peers[0], data)); });
    wait_scheduled(peers[1]);
    fast_msg request{};
    ASSERT_EQ(wait(request), 0L);
    auto sender = launch(
        peers[2], [&] { send_result.store(invoke(SYS_IPC_SEND, peers[0].tid, reinterpret_cast<long>(&legacy))); });
    wait_scheduled(peers[2]);
    exit_peer(peers[0]);
    detach_thread(&peers[0]); // Repeated teardown cannot release pins twice.
    caller.join();
    sender.join();
    ASSERT_EQ(call_result.load(), -32L);
    ASSERT_EQ(send_result.load(), -32L);
    clean_links(peers[0]);
    clean_links(peers[1]);
    clean_links(peers[2]);
}
TEST(cancelled_receivers_drop_enrollment_without_publishing_output) {
    for (long operation : {SYS_IPC_RECV, SYS_IPC_WAIT}) {
        reset();
        message output{99, 98, {97, 96, 95, 94}}, original = output;
        std::atomic<long> result{-999};
        auto receiver = launch(peers[1], [&] { result.store(invoke(operation, reinterpret_cast<long>(&output))); });
        wait_scheduled(peers[1]);
        cancel(peers[1]);
        receiver.join();
        ASSERT_EQ(result.load(), -4L);
        ASSERT(std::memcmp(&output, &original, sizeof(output)) == 0);
        clean_links(peers[1]);
        message data{};
        ASSERT_EQ(ipc_manager::send_async(peers[1].tid, data), -3);
    }
}
TEST(cancelled_middle_caller_preserves_remaining_fifo) {
    reset();
    std::array<fast_msg, 3> data{};
    std::array<std::atomic<long>, 3> results;
    std::array<std::thread, 3> callers;
    for (unsigned i = 0; i < 3; ++i) {
        data[i].d0 = i;
        callers[i] = launch(peers[i + 1], [&, i] { results[i].store(call(peers[0], data[i])); });
        wait_scheduled(peers[i + 1]);
    }
    cancel(peers[2]);
    callers[1].join();
    for (uint64_t expected : {0ULL, 2ULL}) {
        fast_msg request{};
        ASSERT_EQ(wait(request), 0L);
        ASSERT_EQ(request.d0, expected);
        ASSERT_EQ(reply(request), 0L);
    }
    callers[0].join();
    callers[2].join();
    ASSERT_EQ(results[0].load(), 0L);
    ASSERT_EQ(results[1].load(), -4L);
    ASSERT_EQ(results[2].load(), 0L);
    for (unsigned i = 0; i < 4; ++i)
        clean_links(peers[i]);
}
TEST(queue_capacity_rejects_extra_request_and_peer_exit_drains_all_pins) {
    reset();
    std::array<std::thread, MAX_PENDING_REQUESTS> callers;
    std::array<fast_msg, MAX_PENDING_REQUESTS> data{};
    std::array<std::atomic<long>, MAX_PENDING_REQUESTS> results;
    for (unsigned i = 0; i < MAX_PENDING_REQUESTS; ++i) {
        callers[i] = launch(peers[i + 1], [&, i] { results[i].store(call(peers[0], data[i])); });
        wait_scheduled(peers[i + 1]);
    }
    running = &peers[65];
    fast_msg extra{};
    ASSERT_EQ(call(peers[0], extra), -11L);
    exit_peer(peers[0]);
    for (unsigned i = 0; i < MAX_PENDING_REQUESTS; ++i) {
        callers[i].join();
        ASSERT_EQ(results[i].load(), -32L);
        clean_links(peers[i + 1]);
    }
    clean_links(peers[0]);
    clean_links(peers[65]);
}
TEST(peer_pin_survives_exit_between_lookup_and_queue_lock) {
    reset();
    after_pin = [](thread* target) {
        ASSERT_EQ(target->pins.load(), 1U);
        exit_peer(*target);
        ASSERT_EQ(target->pins.load(), 1U);
    };
    message data{};
    ASSERT_EQ(ipc_manager::send_async(peers[1].tid, data), -32);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(authorization_snapshot_releases_pin_and_backend_rejects_other_identity) {
    reset();
    ASSERT(process::ipc_send_allowed(peers[1].tid));
    ASSERT_EQ(peers[1].pins.load(), 0U);
    peers[0].creds.ruid = peers[0].creds.euid = 1000;
    peers[1].creds.ruid = peers[1].creds.euid = 1001;
    message data{};
    fast_msg fixed{};
    ASSERT_EQ(ipc_manager::send_async(peers[1].tid, data), -1);
    ASSERT_EQ(sys_ipc_call(peers[1].tid, &fixed), -1L);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(invalid_self_and_missing_peer_requests_do_not_publish_links) {
    reset();
    message data{};
    fast_msg fixed{};
    ASSERT_EQ(ipc_manager::send_sync(peers[0].tid, data), -35);
    ASSERT_EQ(sys_ipc_call(peers[0].tid, &fixed), -35L);
    ASSERT_EQ(sys_ipc_call(9999, &fixed), -3L);
    ASSERT_EQ(sys_ipc_call(peers[1].tid, nullptr), -14L);
    ASSERT_EQ(sys_ipc_wait(nullptr), -14L);
    ASSERT_EQ(sys_ipc_reply(nullptr), -14L);
    ASSERT_EQ(invoke(SYS_IPC_WAIT, 0), -14L);
    ASSERT_EQ(reply(fixed), -2L);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(cancellation_breaks_a_mutual_call_cycle_without_stale_links) {
    reset();
    fast_msg a{}, b{};
    std::atomic<long> ar{-999}, br{-999};
    auto first = launch(peers[0], [&] { ar.store(call(peers[1], a)); });
    wait_scheduled(peers[0]);
    auto second = launch(peers[1], [&] { br.store(call(peers[0], b)); });
    wait_scheduled(peers[1]);
    cancel(peers[0]);
    first.join();
    second.join();
    ASSERT_EQ(ar.load(), -4L);
    ASSERT_EQ(br.load(), -32L);
    clean_links(peers[0]);
    clean_links(peers[1]);
}
TEST(reply_and_cancellation_race_never_redirects_or_leaks_request) {
    for (unsigned iteration = 0; iteration < 16; ++iteration) {
        reset();
        fast_msg data{1, 2, 3, 4};
        std::atomic<long> result{-999}, reply_result{-999};
        auto caller = launch(peers[1], [&] { result.store(call(peers[0], data)); });
        wait_scheduled(peers[1]);
        fast_msg request{};
        ASSERT_EQ(wait(request), 0L);
        request.d0 = 100;
        std::atomic<bool> start{false};
        auto responder = launch(peers[0], [&] {
            while (!start.load())
                std::this_thread::yield();
            reply_result.store(reply(request));
        });
        auto canceller = std::thread([&] {
            while (!start.load())
                std::this_thread::yield();
            process::signal_manager::post_signal(&peers[1], 15);
        });
        start.store(true);
        responder.join();
        canceller.join();
        caller.join();
        ASSERT(result.load() == 0 || result.load() == -4);
        ASSERT(reply_result.load() == 0 || reply_result.load() == -125);
        ASSERT_EQ(data.d0, result.load() == 0 ? 100ULL : 2ULL);
        clean_links(peers[0]);
        clean_links(peers[1]);
    }
}
TEST(exited_heap_peer_can_be_freed_before_waiting_caller_resumes) {
    reset();
    auto peer = std::make_unique<thread>();
    peer->tid = 100;
    peer->user_entry = reinterpret_cast<void*>(1);
    peer->creds = process::root_credentials();
    peer->state.store(thread_state::RUNNING);
    {
        std::lock_guard guard(registry_lock);
        registry.push_back(peer.get());
    }
    fast_msg data{1, 2, 3, 4};
    std::atomic<long> result{-999};
    auto caller = launch(peers[1], [&] { result.store(call(*peer, data)); });
    wait_scheduled(peers[1]);
    exit_peer(*peer);
    ASSERT_EQ(peer->pins.load(), 0U);
    {
        std::lock_guard guard(registry_lock);
        registry.pop_back();
    }
    peer.reset();
    caller.join();
    ASSERT_EQ(result.load(), -32L);
    clean_links(peers[1]);
}
TEST(cancelled_heap_caller_can_be_freed_before_server_sends_late_reply) {
    reset();
    auto caller_peer = std::make_unique<thread>();
    caller_peer->tid = 101;
    caller_peer->user_entry = reinterpret_cast<void*>(1);
    caller_peer->creds = process::root_credentials();
    caller_peer->state.store(thread_state::RUNNING);
    {
        std::lock_guard guard(registry_lock);
        registry.push_back(caller_peer.get());
    }
    fast_msg data{1, 2, 3, 4};
    std::atomic<long> result{-999};
    auto caller = launch(*caller_peer, [&] { result.store(call(peers[0], data)); });
    wait_scheduled(*caller_peer);
    fast_msg request{};
    ASSERT_EQ(wait(request), 0L);
    cancel(*caller_peer);
    caller.join();
    ASSERT_EQ(result.load(), -4L);
    clean_links(*caller_peer);
    {
        std::lock_guard guard(registry_lock);
        registry.pop_back();
    }
    caller_peer.reset();
    ASSERT_EQ(reply(request), -125L);
    clean_links(peers[0]);
    roundtrip(peers[2], peers[0]);
}

int main() {
    return RUN_ALL_TESTS("IPC lifecycle");
}
