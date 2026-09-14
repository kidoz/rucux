// SPDX-License-Identifier: MIT
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <uapi/kernel/syscalls.h>
#include <unistd.h>
#if defined(__x86_64__)
static int done = 0;
static int failed = 0;
static void check_simd(uint64_t value) {
    uint64_t expected[2] = {value, ~value}, actual[2]{};
    for (int i = 0; i < 300; ++i) {
        long number = SYS_YIELD;
        asm volatile("movdqu %2, %%xmm15; syscall; movdqu %%xmm15, %0"
                     : "=m"(actual), "+a"(number)
                     : "m"(expected)
                     : "rcx", "r11", "xmm15", "memory");
        if (actual[0] != expected[0] || actual[1] != expected[1]) __atomic_store_n(&failed, 1, __ATOMIC_RELEASE);
    }
}
static void* worker(void*) {
    check_simd(0x123456789abcdef0ULL);
    __atomic_store_n(&done, 1, __ATOMIC_RELEASE);
    return nullptr;
}
#endif
int main() {
    timespec before{}, after{}, invalid{0, 1000000000};
    if (syscall(SYS_NANOSLEEP, reinterpret_cast<long>(&invalid), 0) != -22) return 1;
    if (clock_gettime(CLOCK_MONOTONIC, &before) < 0) return 2;
    usleep(250000);
    if (clock_gettime(CLOCK_MONOTONIC, &after) < 0) return 3;
    long elapsed = (after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000;
    if (elapsed < 250 || elapsed > 2000) return 4;
    printf("terminal_smoke: timer sleep PASS\n");
#if defined(__x86_64__)
    pthread_t thread;
    if (pthread_create(&thread, nullptr, worker, nullptr) != 0) return 5;
    check_simd(0xfedcba9876543210ULL);
    while (!__atomic_load_n(&done, __ATOMIC_ACQUIRE))
        syscall(SYS_YIELD);
    if (__atomic_load_n(&failed, __ATOMIC_ACQUIRE)) return 6;
    printf("terminal_smoke: SIMD context PASS\n");
#endif
    return 7; // The parent must receive this exact exit status through waitpid.
}
