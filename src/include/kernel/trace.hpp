// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <uapi/kernel/trace.h>

namespace kernel::trace {

void init() noexcept;
void set_enabled(bool enabled) noexcept;
bool is_enabled() noexcept;
void reset() noexcept;

uint64_t clock_now() noexcept;
uint32_t clock_id() noexcept;
uint64_t clock_freq_hz() noexcept;

void record(uint16_t type, uint16_t event, uint64_t arg0 = 0, uint64_t arg1 = 0) noexcept;
void record_crash(uint16_t type, uint16_t event, uint64_t arg0 = 0, uint64_t arg1 = 0) noexcept;
void dump_crash_ring() noexcept;
long sys_trace_ctl(uint32_t op, void* arg0, size_t arg1, uintptr_t arg2) noexcept;

} // namespace kernel::trace

#define TRACE_INSTANT(event, arg0, arg1) ::kernel::trace::record(TRACE_RECORD_INSTANT, event, arg0, arg1)
#define TRACE_CRASH_INSTANT(event, arg0, arg1) ::kernel::trace::record_crash(TRACE_RECORD_INSTANT, event, arg0, arg1)
