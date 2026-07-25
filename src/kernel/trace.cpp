// SPDX-License-Identifier: MIT
#include <kernel/trace.hpp>

#if defined(__arm__)
#include <arch/armv7/timer.hpp>
#endif

#include <kernel/cpu/percpu.hpp>
#include <kernel/print.hpp>
#include <kernel/scheduler/scheduler.hpp>
#include <kernel/sync/atomic.hpp>
#include <lib/string.hpp>

namespace kernel::trace {

static constexpr uint32_t TRACE_BUFFER_CAPACITY = 256;

struct trace_slot {
    trace_record record;
    uint64_t committed_seq;
};

struct trace_cpu_buffer {
    kernel::atomic<uint64_t> write_pos;
    trace_slot slots[TRACE_BUFFER_CAPACITY];
};

static trace_cpu_buffer g_buffers[kernel::cpu::MAX_CPUS];
static kernel::atomic<uint32_t> g_enabled{0};

static constexpr uint32_t CRASH_RING_MAGIC = 0x43524153; // "CRAS"
static constexpr uint32_t CRASH_BUFFER_CAPACITY = 64;

struct crash_ring {
    uint32_t magic;
    kernel::atomic<uint32_t> write_pos;
    trace_record slots[CRASH_BUFFER_CAPACITY];
};

// Placing this in a section that ideally doesn't get zeroed, though BSS usually does.
// For now, we rely on the magic number to check if it survived a soft reboot.
__attribute__((section(".noinit"))) static crash_ring g_crash_ring;

static void clear_slot(trace_slot& slot) noexcept {
    lib::memset(&slot.record, 0, sizeof(slot.record));
    __atomic_store_n(&slot.committed_seq, 0ULL, __ATOMIC_RELAXED);
}

void init() noexcept {
    dump_crash_ring();
    g_crash_ring.magic = CRASH_RING_MAGIC;
    g_crash_ring.write_pos.store(0, kernel::relaxed);

    reset();
    set_enabled(true);
    TRACE_INSTANT(TRACE_EVENT_BOOT_STAGE, 1, 0);
}

void set_enabled(bool enabled) noexcept {
    g_enabled.store(enabled ? 1U : 0U, kernel::release);
}

bool is_enabled() noexcept {
    return g_enabled.load(kernel::acquire) != 0;
}

void reset() noexcept {
    for (uint32_t cpu = 0; cpu < kernel::cpu::MAX_CPUS; ++cpu) {
        g_buffers[cpu].write_pos.store(0, kernel::relaxed);
        for (uint32_t i = 0; i < TRACE_BUFFER_CAPACITY; ++i)
            clear_slot(g_buffers[cpu].slots[i]);
    }
}

uint64_t clock_now() noexcept {
#if defined(__x86_64__)
    uint32_t lo = 0;
    uint32_t hi = 0;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__arm__)
    return arch::armv7::generic_timer::read_counter();
#else
    return 0;
#endif
}

uint32_t clock_id() noexcept {
#if defined(__x86_64__)
    return TRACE_CLOCK_TSC_RAW;
#elif defined(__arm__)
    return TRACE_CLOCK_CNTVCT_RAW;
#else
    return TRACE_CLOCK_NONE;
#endif
}

uint64_t clock_freq_hz() noexcept {
#if defined(__arm__)
    return arch::armv7::generic_timer::get_frequency();
#else
    return 0;
#endif
}

static uint32_t current_thread_id() noexcept {
    auto* pcpu = kernel::cpu::this_cpu();
    if (!pcpu) return 0;
    auto* t = pcpu->current_thread;
    return t ? t->tid : 0;
}

void record(uint16_t type, uint16_t event, uint64_t arg0, uint64_t arg1) noexcept {
    if (!is_enabled()) return;

    auto* pcpu = kernel::cpu::this_cpu();
    if (!pcpu || pcpu->cpu_id >= kernel::cpu::MAX_CPUS) return;

    trace_cpu_buffer& buffer = g_buffers[pcpu->cpu_id];
    uint64_t seq = buffer.write_pos.fetch_add(1, kernel::relaxed);
    trace_slot& slot = buffer.slots[seq % TRACE_BUFFER_CAPACITY];

    __atomic_store_n(&slot.committed_seq, 0ULL, __ATOMIC_RELAXED);
    slot.record.timestamp = clock_now();
    slot.record.seq_no = seq;
    slot.record.arg0 = arg0;
    slot.record.arg1 = arg1;
    slot.record.cpu_id = pcpu->cpu_id;
    slot.record.thread_id = current_thread_id();
    slot.record.type = type;
    slot.record.event = event;
    slot.record.reserved = 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&slot.committed_seq, seq + 1, __ATOMIC_RELEASE);
}

void record_crash(uint16_t type, uint16_t event, uint64_t arg0, uint64_t arg1) noexcept {
    if (g_crash_ring.magic != CRASH_RING_MAGIC) return;

    auto* pcpu = kernel::cpu::this_cpu();
    uint32_t cpu_id = (pcpu && pcpu->cpu_id < kernel::cpu::MAX_CPUS) ? pcpu->cpu_id : 0xFFFFFFFF;

    uint32_t seq = g_crash_ring.write_pos.fetch_add(1, kernel::relaxed);
    trace_record& rec = g_crash_ring.slots[seq % CRASH_BUFFER_CAPACITY];

    rec.timestamp = clock_now();
    rec.seq_no = seq;
    rec.arg0 = arg0;
    rec.arg1 = arg1;
    rec.cpu_id = cpu_id;
    rec.thread_id = current_thread_id();
    rec.type = type;
    rec.event = event;
    rec.reserved = 0;
}

void dump_crash_ring() noexcept {
    if (g_crash_ring.magic != CRASH_RING_MAGIC) {
        return;
    }

    uint32_t written = g_crash_ring.write_pos.load(kernel::relaxed);
    if (written == 0) return;

    kernel::print("=== CRASH TRACE RING DUMP ===\n");
    uint32_t start = (written > CRASH_BUFFER_CAPACITY) ? (written - CRASH_BUFFER_CAPACITY) : 0;
    for (uint32_t seq = start; seq < written; ++seq) {
        const trace_record& rec = g_crash_ring.slots[seq % CRASH_BUFFER_CAPACITY];
        kernel::print("[%u] CPU %u TID %u TYPE %u EVENT %u ARG0 0x%x ARG1 0x%x\n",
                      static_cast<uint32_t>(rec.timestamp & 0xFFFFFFFF), rec.cpu_id, rec.thread_id, rec.type, rec.event,
                      rec.arg0, rec.arg1);
    }
    kernel::print("=============================\n");
}

static void fill_stats(trace_stats& stats) noexcept {
    stats.enabled = is_enabled() ? 1U : 0U;
    stats.clock_id = clock_id();
    stats.cpu_count = kernel::cpu::g_cpu_count.load(kernel::acquire);
    stats.record_capacity_per_cpu = TRACE_BUFFER_CAPACITY;
    stats.clock_freq_hz = clock_freq_hz();
    stats.records_written = 0;
    stats.records_overwritten = 0;
    stats.records_available = 0;

    for (uint32_t cpu = 0; cpu < stats.cpu_count && cpu < kernel::cpu::MAX_CPUS; ++cpu) {
        uint64_t written = g_buffers[cpu].write_pos.load(kernel::acquire);
        stats.records_written += written;
        if (written > TRACE_BUFFER_CAPACITY) stats.records_overwritten += written - TRACE_BUFFER_CAPACITY;
        stats.records_available += (written < TRACE_BUFFER_CAPACITY) ? written : TRACE_BUFFER_CAPACITY;
    }
}

static long snapshot(void* buffer, size_t size) noexcept {
    if (!buffer || size < sizeof(trace_snapshot_header)) return -1;

    auto* header = static_cast<trace_snapshot_header*>(buffer);
    auto* out = reinterpret_cast<trace_record*>(header + 1);
    size_t max_records = (size - sizeof(trace_snapshot_header)) / sizeof(trace_record);
    size_t out_count = 0;

    trace_stats stats{};
    fill_stats(stats);

    for (uint32_t cpu = 0; cpu < stats.cpu_count && cpu < kernel::cpu::MAX_CPUS; ++cpu) {
        uint64_t written = g_buffers[cpu].write_pos.load(kernel::acquire);
        uint64_t start = (written > TRACE_BUFFER_CAPACITY) ? (written - TRACE_BUFFER_CAPACITY) : 0;

        for (uint64_t seq = start; seq < written && out_count < max_records; ++seq) {
            trace_slot& slot = g_buffers[cpu].slots[seq % TRACE_BUFFER_CAPACITY];
            uint64_t committed = __atomic_load_n(&slot.committed_seq, __ATOMIC_ACQUIRE);
            if (committed != seq + 1) continue;
            out[out_count++] = slot.record;
        }
    }

    header->magic = TRACE_SNAPSHOT_MAGIC;
    header->version = TRACE_SNAPSHOT_VERSION;
    header->header_size = sizeof(trace_snapshot_header);
    header->clock_id = stats.clock_id;
    header->cpu_count = stats.cpu_count;
    header->record_size = sizeof(trace_record);
    header->record_count = static_cast<uint32_t>(out_count);
    header->clock_freq_hz = stats.clock_freq_hz;
    header->records_written = stats.records_written;
    header->records_overwritten = stats.records_overwritten;
    return static_cast<long>(sizeof(trace_snapshot_header) + (out_count * sizeof(trace_record)));
}

long sys_trace_ctl(uint32_t op, void* arg0, size_t arg1, uintptr_t arg2) noexcept {
    (void)arg2;

    switch (op) {
    case TRACE_CTL_RESET:
        reset();
        return 0;
    case TRACE_CTL_ENABLE:
        set_enabled(true);
        return 0;
    case TRACE_CTL_DISABLE:
        set_enabled(false);
        return 0;
    case TRACE_CTL_SNAPSHOT:
        return snapshot(arg0, arg1);
    case TRACE_CTL_STATS:
        if (!arg0 || arg1 < sizeof(trace_stats)) return -1;
        fill_stats(*static_cast<trace_stats*>(arg0));
        return 0;
    default:
        return -1;
    }
}

} // namespace kernel::trace
