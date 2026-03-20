// SPDX-License-Identifier: MIT
#include <kernel/console.hpp>
#include <kernel/print.hpp>

namespace kernel::console {
namespace {

constexpr uint32_t MAX_SINKS = 4;
constexpr display_info DEFAULT_DISPLAY_INFO = {80, 25, 0, 0, false};

sink g_sinks[MAX_SINKS] = {};
uint32_t g_sink_count = 0;
int32_t g_display_sink = -1;

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

void putc(char c) noexcept {
    for (uint32_t i = 0; i < g_sink_count; ++i) {
        g_sinks[i].putc(c);
    }
}

void write(const char* s) noexcept {
    if (!s) {
        return;
    }

    for (; *s != '\0'; ++s) {
        putc(*s);
    }
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

void kputc(char c) noexcept {
    console::putc(c);
}

void kwrite(const char* s) noexcept {
    console::write(s);
}

} // namespace kernel
