// SPDX-License-Identifier: MIT
// Minimal test harness for host-compiled kernel unit tests.
// No framework dependencies — just assert + colored output.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)                                         \
    static void test_##name();                             \
    static struct test_reg_##name {                        \
        test_reg_##name() {                                \
            g_tests_run++;                                 \
            printf("  %-50s ", #name);                     \
            try {                                          \
                test_##name();                             \
                g_tests_passed++;                          \
                printf("\033[32mPASS\033[0m\n");           \
            } catch (...) {                                \
                g_tests_failed++;                          \
                printf("\033[31mFAIL\033[0m\n");           \
            }                                              \
        }                                                  \
    } g_test_instance_##name;                              \
    static void test_##name()

#define ASSERT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("\n    ASSERT FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw 1;                                                         \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(a, b)                                                      \
    do {                                                                     \
        auto _a = (a); auto _b = (b);                                       \
        if (_a != _b) {                                                      \
            printf("\n    ASSERT_EQ FAILED: %s != %s (%s:%d)\n",             \
                   #a, #b, __FILE__, __LINE__);                              \
            throw 1;                                                         \
        }                                                                    \
    } while (0)

static inline int test_summary() {
    printf("\n%d tests: %d passed, %d failed\n", g_tests_run, g_tests_passed, g_tests_failed);
    return g_tests_failed ? 1 : 0;
}
