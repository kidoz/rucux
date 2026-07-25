// SPDX-License-Identifier: MIT
#include <kernel/console.hpp>
#include <kernel/print.hpp>
#include <kernel/sync/spinlock.hpp>

namespace kernel::console {
namespace {

constexpr uint32_t MAX_SINKS = 4;
constexpr display_info DEFAULT_DISPLAY_INFO = {80, 25, 0, 0, false};

sink g_sinks[MAX_SINKS] = {};
uint32_t g_sink_count = 0;
int32_t g_display_sink = -1;

// Serializes output across CPUs. Without it, concurrent writes from secondary
// cores interleave mid-line and corrupt the log — which also breaks any test
// that greps for a marker. IRQ-safe because console output happens from
// interrupt handlers as well as thread context.
kernel::irq_spinlock g_console_lock;

} // namespace

void reset() noexcept {
    g_sink_count = 0;
    g_display_sink = -1;
    for (uint32_t i = 0; i < MAX_SINKS; ++i) {
        g_sinks[i] = {};
    }
}

bool register_sink(sink new_sink) noexcept {
    if (!new_sink.putc || g_sink_count >= MAX_SINKS) {
        return false;
    }

    g_sinks[g_sink_count] = new_sink;
    if (new_sink.get_display_info) {
        g_display_sink = static_cast<int32_t>(g_sink_count);
    }
    ++g_sink_count;
    return true;
}

uintptr_t lock_output() noexcept {
    return g_console_lock.lock();
}

void unlock_output(uintptr_t flags) noexcept {
    g_console_lock.unlock(flags);
}

// Caller must already hold the console lock.
void putc_unlocked(char c) noexcept {
    for (uint32_t i = 0; i < g_sink_count; ++i) {
        g_sinks[i].putc(c);
    }
}

void putc(char c) noexcept {
    uintptr_t flags = g_console_lock.lock();
    putc_unlocked(c);
    g_console_lock.unlock(flags);
}

void write(const char* s) noexcept {
    if (!s) {
        return;
    }

    // Locked once for the whole string so a line cannot be split by another CPU.
    uintptr_t flags = g_console_lock.lock();
    for (; *s != '\0'; ++s) {
        putc_unlocked(*s);
    }
    g_console_lock.unlock(flags);
}

display_info get_display_info() noexcept {
    if (g_display_sink >= 0) {
        display_info info = g_sinks[g_display_sink].get_display_info();
        if (info.available) {
            return info;
        }
    }

    return DEFAULT_DISPLAY_INFO;
}

} // namespace kernel::console

namespace kernel {

// These run underneath kernel::print(), which already holds the console lock,
// so they must use the unlocked path or the spinlock would deadlock on itself.
void kputc(char c) noexcept {
    console::putc_unlocked(c);
}

void kwrite(const char* s) noexcept {
    if (!s) return;
    for (; *s != '\0'; ++s)
        console::putc_unlocked(*s);
}

} // namespace kernel
