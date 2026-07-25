// SPDX-License-Identifier: MIT
// Host-compiled test framework for kernel unit tests.
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace rucux_test {

struct TestInfo {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestInfo>& get_registry() {
    static std::vector<TestInfo> registry;
    return registry;
}

class TestRegistrar {
public:
    TestRegistrar(const char* name, std::function<void()> func) { get_registry().push_back({name, func}); }
};

struct AssertionFailure : public std::exception {
    std::string msg;
    AssertionFailure(const std::string& m)
        : msg(m) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

inline int run_all_tests(const char* suite_name) {
    int passed = 0;
    int failed = 0;
    auto& reg = get_registry();

    std::cout << "[==========] Running " << reg.size() << " tests for " << suite_name << ".\n";

    for (const auto& test : reg) {
        std::cout << "[ RUN      ] " << test.name << "\n";
        try {
            test.func();
            std::cout << "[       OK ] " << test.name << "\n";
            passed++;
        } catch (const AssertionFailure& e) {
            std::cout << "    " << e.what() << "\n";
            std::cout << "[  FAILED  ] " << test.name << "\n";
            failed++;
        } catch (...) {
            std::cout << "    Unknown exception caught\n";
            std::cout << "[  FAILED  ] " << test.name << "\n";
            failed++;
        }
    }

    std::cout << "[==========] " << reg.size() << " tests ran.\n";
    std::cout << "[  PASSED  ] " << passed << " tests.\n";
    if (failed > 0) {
        std::cout << "[  FAILED  ] " << failed << " tests.\n";
        return 1;
    }
    return 0;
}

} // namespace rucux_test

#define TEST(name)                                                                                                     \
    static void test_func_##name();                                                                                    \
    static rucux_test::TestRegistrar test_reg_##name(#name, test_func_##name);                                         \
    static void test_func_##name()

#define ASSERT(cond)                                                                                                   \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::ostringstream _ss;                                                                                    \
            _ss << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__;                             \
            throw rucux_test::AssertionFailure(_ss.str());                                                             \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ(a, b)                                                                                                \
    do {                                                                                                               \
        auto _a = (a);                                                                                                 \
        auto _b = (b);                                                                                                 \
        if (_a != _b) {                                                                                                \
            std::ostringstream _ss;                                                                                    \
            _ss << "Assertion failed: " << #a << " == " << #b << " at " << __FILE__ << ":" << __LINE__;                \
            throw rucux_test::AssertionFailure(_ss.str());                                                             \
        }                                                                                                              \
    } while (0)

#define RUN_ALL_TESTS(suite_name) rucux_test::run_all_tests(suite_name)
