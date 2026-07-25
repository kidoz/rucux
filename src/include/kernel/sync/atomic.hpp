// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kernel {

enum memory_order {
    relaxed = __ATOMIC_RELAXED,
    consume = __ATOMIC_CONSUME,
    acquire = __ATOMIC_ACQUIRE,
    release = __ATOMIC_RELEASE,
    acq_rel = __ATOMIC_ACQ_REL,
    seq_cst = __ATOMIC_SEQ_CST
};

template <typename T>
class atomic {
    static_assert(__is_trivially_copyable(T), "atomic requires trivially copyable type");

    T value_{};

public:
    constexpr atomic() noexcept = default;
    constexpr explicit atomic(T val) noexcept
        : value_{val} {}

    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;

    T load(memory_order order = seq_cst) const noexcept { return __atomic_load_n(&value_, order); }

    void store(T val, memory_order order = seq_cst) noexcept { __atomic_store_n(&value_, val, order); }

    T exchange(T val, memory_order order = seq_cst) noexcept { return __atomic_exchange_n(&value_, val, order); }

    T fetch_add(T val, memory_order order = seq_cst) noexcept { return __atomic_fetch_add(&value_, val, order); }

    T fetch_sub(T val, memory_order order = seq_cst) noexcept { return __atomic_fetch_sub(&value_, val, order); }

    T fetch_and(T val, memory_order order = seq_cst) noexcept { return __atomic_fetch_and(&value_, val, order); }

    T fetch_or(T val, memory_order order = seq_cst) noexcept { return __atomic_fetch_or(&value_, val, order); }

    bool compare_exchange_strong(T& expected, T desired, memory_order success = seq_cst,
                                 memory_order failure = seq_cst) noexcept {
        return __atomic_compare_exchange_n(&value_, &expected, desired, false, success, failure);
    }

    bool compare_exchange_weak(T& expected, T desired, memory_order success = seq_cst,
                               memory_order failure = seq_cst) noexcept {
        return __atomic_compare_exchange_n(&value_, &expected, desired, true, success, failure);
    }
};

inline void atomic_thread_fence(memory_order order) noexcept {
    __atomic_thread_fence(order);
}

} // namespace kernel
