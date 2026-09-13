// SPDX-License-Identifier: MIT
#include <kernel/memory/user_access.hpp>
#include <kernel/memory/vmm.hpp>
#include <lib/string.hpp>

namespace kernel::memory {

irq_spinlock user_mapping_lock;

static bool accessible(uintptr_t address, size_t size, bool write) noexcept {
    if (size == 0) return true;
    if (!user_range(address, size)) return false;
    while (size) {
        if (!vmm::get_user_phys(address, write)) return false;
        const size_t room = 4096 - (address & 4095);
        const size_t count = size < room ? size : room;
        address += count;
        size -= count;
    }
    return true;
}

bool user_accessible(const void* address, size_t size, bool write) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    return accessible(reinterpret_cast<uintptr_t>(address), size, write);
}

static bool copy(void* dest, const void* source, size_t size, bool to_user) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    uintptr_t address = reinterpret_cast<uintptr_t>(to_user ? dest : source);
    if (!accessible(address, size, to_user)) return false;
    auto* kernel_buffer = static_cast<uint8_t*>(to_user ? const_cast<void*>(source) : dest);
    while (size) {
        const uintptr_t phys = vmm::get_user_phys(address, to_user);
        const size_t room = 4096 - (address & 4095);
        const size_t count = size < room ? size : room;
        // The mapping lock prevents concurrent unmap/free until copying ends.
        // Only the checked physical frame is dereferenced, never the user VA.
        if (to_user)
            lib::memcpy(reinterpret_cast<void*>(phys), kernel_buffer, count);
        else
            lib::memcpy(kernel_buffer, reinterpret_cast<const void*>(phys), count);
        address += count;
        kernel_buffer += count;
        size -= count;
    }
    return true;
}

bool copy_from_user(void* dest, const void* source, size_t size) noexcept {
    return copy(dest, source, size, false);
}
bool copy_to_user(void* dest, const void* source, size_t size) noexcept {
    return copy(dest, source, size, true);
}

long copy_user_string(char* dest, const char* source, size_t capacity) noexcept {
    irq_lock_guard guard(user_mapping_lock);
    uintptr_t address = reinterpret_cast<uintptr_t>(source);
    for (size_t i = 0; i < capacity; ++i, ++address) {
        if (!user_range(address, 1)) return -14;
        uintptr_t phys = vmm::get_user_phys(address, false);
        if (!phys) return -14;
        dest[i] = *reinterpret_cast<const char*>(phys);
        if (!dest[i]) return static_cast<long>(i);
    }
    return -36;
}

} // namespace kernel::memory
