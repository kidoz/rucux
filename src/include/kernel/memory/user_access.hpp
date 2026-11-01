// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/spinlock.hpp>
#include <lib/stddef.hpp>
#include <stdint.h>

namespace kernel::memory {

// User mappings must never overlap shared kernel tables or identity mappings.
#if defined(__x86_64__)
inline constexpr uintptr_t USER_BEGIN = 0x8000000000ULL;
inline constexpr uintptr_t USER_END = 0x800000000000ULL;
#elif defined(__aarch64__)
inline constexpr uintptr_t USER_BEGIN = 0x100000000ULL;
inline constexpr uintptr_t USER_END = 0x8000000000ULL;
#else
// ARMv7 has no supported userspace product yet; reserve a separate L1 slot.
inline constexpr uintptr_t USER_BEGIN = 0x80000000UL;
inline constexpr uintptr_t USER_END = 0xC0000000UL;
#endif

inline bool user_range(uintptr_t address, size_t size) noexcept {
    return address >= USER_BEGIN && address < USER_END && size <= USER_END - address;
}

// Serializes page-table mutation/walks, physical copies and hardware root
// switches. Lock order: thread-list or mm -> this lock -> allocator. Never held
// across I/O, scheduling or a blocking wait; TLB completion uses hardware/NMI
// acknowledgements that do not acquire this lock.
extern irq_spinlock user_mapping_lock;
bool user_accessible(const void* address, size_t size, bool write) noexcept;
bool copy_from_user(void* dest, const void* source, size_t size) noexcept;
bool copy_to_user(void* dest, const void* source, size_t size) noexcept;
long copy_user_string(char* dest, const char* source, size_t capacity) noexcept;

} // namespace kernel::memory
