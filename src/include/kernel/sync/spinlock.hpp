// SPDX-License-Identifier: MIT
#pragma once
#include <kernel/sync/atomic.hpp>
#include <stdint.h>

namespace kernel {

// Architecture-specific pause hint for busy-wait loops
inline void cpu_relax() noexcept {
#if defined(__x86_64__)
    asm volatile("pause" ::: "memory");
#elif defined(__arm__) || defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    asm volatile("" ::: "memory");
#endif
}

// Save interrupt flags and disable interrupts.
// In hosted (non-freestanding) builds, these are no-ops for testability.
inline uintptr_t irq_save() noexcept {
    uintptr_t flags = 0;
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
  #if defined(__x86_64__)
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
  #elif defined(__arm__)
    asm volatile("mrs %0, cpsr; cpsid i" : "=r"(flags) :: "memory");
  #elif defined(__aarch64__)
    asm volatile("mrs %0, daif; msr daifset, #0xf" : "=r"(flags) :: "memory");
  #endif
#endif
    return flags;
}

// Restore previously saved interrupt flags
inline void irq_restore([[maybe_unused]] uintptr_t flags) noexcept {
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0
  #if defined(__x86_64__)
    asm volatile("push %0; popfq" :: "g"(flags) : "memory", "cc");
  #elif defined(__arm__)
    asm volatile("msr cpsr_c, %0" :: "r"(flags) : "memory");
  #elif defined(__aarch64__)
    asm volatile("msr daif, %0" :: "r"(flags) : "memory");
  #endif
#endif
}

// Fair ticket spinlock — guarantees FIFO ordering among waiters
class spinlock {
    atomic<uint16_t> next_ticket_{0};
    atomic<uint16_t> now_serving_{0};

public:
    constexpr spinlock() noexcept = default;

    void lock() noexcept {
        uint16_t ticket = next_ticket_.fetch_add(1, relaxed);
        while (now_serving_.load(acquire) != ticket) {
            cpu_relax();
        }
    }

    void unlock() noexcept {
        now_serving_.fetch_add(1, release);
    }

    bool try_lock() noexcept {
        uint16_t expected = now_serving_.load(relaxed);
        return next_ticket_.compare_exchange_strong(expected, expected + 1,
                                                     acquire, relaxed);
    }

    bool is_locked() const noexcept {
        return next_ticket_.load(relaxed) != now_serving_.load(relaxed);
    }
};

// Interrupt-safe spinlock: disables local IRQs + acquires spinlock.
// On uniprocessor this is just cli/sti. On SMP the spinlock serializes across CPUs.
class irq_spinlock {
    spinlock inner_;

public:
    constexpr irq_spinlock() noexcept = default;

    // Returns saved interrupt flags — caller must pass to unlock()
    [[nodiscard]] uintptr_t lock() noexcept {
        uintptr_t flags = irq_save();
        inner_.lock();
        return flags;
    }

    void unlock(uintptr_t flags) noexcept {
        inner_.unlock();
        irq_restore(flags);
    }
};

// RAII guard for irq_spinlock
class irq_lock_guard {
    irq_spinlock& lock_;
    uintptr_t flags_;

public:
    explicit irq_lock_guard(irq_spinlock& lock) noexcept
        : lock_{lock}, flags_{lock.lock()} {}

    ~irq_lock_guard() noexcept { lock_.unlock(flags_); }

    irq_lock_guard(const irq_lock_guard&) = delete;
    irq_lock_guard& operator=(const irq_lock_guard&) = delete;
};

} // namespace kernel
