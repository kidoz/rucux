// SPDX-License-Identifier: MIT
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <uapi/kernel/syscalls.h>
#include <uapi/kernel/top.h>
#include <unistd.h>

namespace {
constexpr unsigned WORKERS = 4, ROUNDS = 16, WAVES = 4;
constexpr size_t PAGE = 4096, STACK = 65536;
constexpr uintptr_t BASE = 0x102000000ULL;
constexpr unsigned FLAGS = static_cast<unsigned>(MAP_PRIVATE) | static_cast<unsigned>(MAP_ANONYMOUS);
constexpr unsigned FIXED_FLAGS = FLAGS | static_cast<unsigned>(MAP_FIXED);
constexpr unsigned READ_WRITE = static_cast<unsigned>(PROT_READ) | static_cast<unsigned>(PROT_WRITE);
unsigned phase = 0, failed = 0;
unsigned ready[WORKERS]{}, paused[WORKERS]{};
uint64_t stamp(unsigned epoch) {
    return 0x1234000000000000ULL | epoch;
}
unsigned load(const unsigned* word) {
    return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}
void store(unsigned* word, unsigned value) {
    __atomic_store_n(word, value, __ATOMIC_RELEASE);
}
void yield() {
    syscall(SYS_YIELD);
}
auto* const data = reinterpret_cast<volatile uint64_t*>(BASE);

void worker(void* arg) {
    const unsigned index = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
    for (unsigned round = 1; round <= ROUNDS; ++round) {
        const unsigned start = round * 2 - 1;
        while (load(&phase) < start)
            yield();
        if (*data != stamp(round)) store(&failed, 1);
        // Concurrent leaf/table creation and removal in sibling-owned ranges.
        auto* scratch = reinterpret_cast<void*>(BASE + 0x40000000ULL + index * 0x200000ULL);
        if (mmap(scratch, PAGE, READ_WRITE, FIXED_FLAGS, -1, 0) != scratch) {
            store(&failed, 1);
        } else {
            *static_cast<volatile uint64_t*>(scratch) = stamp(index + 100);
            if (*static_cast<volatile uint64_t*>(scratch) != stamp(index + 100) || munmap(scratch, PAGE) != 0)
                store(&failed, 1);
        }
        store(&ready[index], round);
        while (load(&phase) == start) {
            if (*data != stamp(round)) store(&failed, 1);
            // Keep translations hot across permission updates; a BBM fault
            // must wait for the completed operation and retry a permitted read.
        }
        store(&paused[index], round);
    }
    _exit(0);
}
bool all(const unsigned* counters, unsigned value) {
    for (unsigned i = 0; i < WORKERS; ++i)
        if (load(&counters[i]) != value) return false;
    return true;
}
bool reaped(long tid) {
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        top_info info{};
        if (syscall(SYS_TOP, reinterpret_cast<long>(&info), sizeof(info)) != 0) return false;
        bool found = false;
        for (unsigned i = 0; i < info.num_processes; ++i)
            if (info.processes[i].tid == static_cast<uint32_t>(tid)) found = true;
        if (!found) return true;
        yield();
    }
    return false;
}
bool run() {
    for (unsigned wave = 0; wave < WAVES; ++wave) {
        store(&phase, 0);
        void* stacks[WORKERS]{};
        long tids[WORKERS]{};
        for (unsigned i = 0; i < WORKERS; ++i) {
            store(&ready[i], 0);
            store(&paused[i], 0);
            stacks[i] = mmap(nullptr, STACK, READ_WRITE, FLAGS, -1, 0);
            if (stacks[i] == MAP_FAILED) return false;
            tids[i] = syscall(SYS_CLONE, reinterpret_cast<long>(worker),
                              reinterpret_cast<long>(static_cast<uint8_t*>(stacks[i]) + STACK), i);
            if (tids[i] < 0) return false;
        }
        void* poison = MAP_FAILED;
        for (unsigned round = 1; round <= ROUNDS; ++round) {
            if (mmap(reinterpret_cast<void*>(BASE), PAGE, READ_WRITE, FIXED_FLAGS, -1, 0) !=
                reinterpret_cast<void*>(BASE))
                return false;
            if (*data != 0) return false;
            *data = stamp(round);
            store(&phase, round * 2 - 1);
            while (!all(ready, round))
                yield();
            for (unsigned pass = 0; pass < 8; ++pass) {
                if (mprotect(reinterpret_cast<void*>(BASE), PAGE, PROT_READ) != 0) return false;
                // Kernel copyout must see the same new permissions. Keep the
                // clock result away from the word readers continuously check.
                if (syscall(SYS_CLOCK_GETTIME, 1, BASE + 128) != -14) return false;
                if (mprotect(reinterpret_cast<void*>(BASE), PAGE, READ_WRITE) != 0 ||
                    syscall(SYS_CLOCK_GETTIME, 1, BASE + 128) != 0)
                    return false;
            }
            store(&phase, round * 2);
            while (!all(paused, round))
                yield();
            if (load(&failed)) return false;
            if (poison != MAP_FAILED && munmap(poison, PAGE) != 0) return false;
            if (munmap(reinterpret_cast<void*>(BASE), PAGE) != 0) return false;
            // Hold a recycled frame at another VA and poison it. A stale TLB
            // entry must not make the next round read this old frame.
            poison = mmap(nullptr, PAGE, READ_WRITE, FLAGS, -1, 0);
            if (poison == MAP_FAILED) return false;
            *static_cast<volatile uint64_t*>(poison) = 0xDEADDEADDEADDEADULL;
        }
        if (munmap(poison, PAGE) != 0) return false;
        for (unsigned i = 0; i < WORKERS; ++i)
            if (!reaped(tids[i]) || munmap(stacks[i], STACK) != 0) return false;
    }
    return !load(&failed);
}
} // namespace
int main() {
    const bool ok = run();
    const char pass[] = "shared mm: userspace map/protect/reuse/clone PASS\n";
    const char fail[] = "shared mm: FAIL\n";
    write(1, ok ? pass : fail, ok ? sizeof(pass) - 1 : sizeof(fail) - 1);
    return ok ? 0 : 1;
}
