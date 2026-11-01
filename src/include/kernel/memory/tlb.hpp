// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/spinlock.hpp>
#include <stdint.h>

namespace kernel::memory::tlb {
inline constexpr uint32_t CPU_LIMIT = 64;
// Protected by user_mapping_lock, including the hardware root switch. No PCIDs
// or ASIDs are retained across switches. CPUs cannot join a root mid-shootdown.
class root_set {
public:
    void enter(uint32_t cpu, uintptr_t root) noexcept { roots_[cpu] = root; }
    uint64_t mask(uintptr_t root) const noexcept {
        uint64_t result = 0;
        if (root)
            for (uint32_t i = 0; i < CPU_LIMIT; ++i)
                if (roots_[i] == root) result |= uint64_t{1} << i;
        return result;
    }
    uint64_t all() const noexcept {
        uint64_t result = 0;
        for (uint32_t i = 0; i < CPU_LIMIT; ++i)
            if (roots_[i]) result |= uint64_t{1} << i;
        return result;
    }

private:
    uintptr_t roots_[CPU_LIMIT]{};
};
extern root_set active;
uint32_t current_cpu() noexcept;

// One producer (mapping lock) and one lock-free interrupt consumer per CPU.
// Payload may be reused only after completion; acknowledgement is its last use.
class mailbox {
public:
    uint32_t post(uintptr_t address) noexcept {
        address_ = address;
        const uint32_t next = requested_.load(relaxed) + 1;
        requested_.store(next, release);
        return next;
    }
    template <class Flush>
    bool service(Flush flush) noexcept {
        const uint32_t sequence = requested_.load(acquire);
        if (sequence == completed_.load(relaxed)) return false;
        flush(address_);
        completed_.store(sequence, release);
        return true;
    }
    bool complete(uint32_t sequence) const noexcept { return completed_.load(acquire) == sequence; }

private:
    uintptr_t address_ = 0;
    atomic<uint32_t> requested_{0}, completed_{0};
};
} // namespace kernel::memory::tlb
