// SPDX-License-Identifier: MIT
// Link the production implementations under private names to keep the host
// test harness on its own libc. No host result is used as the implementation.
#include "test_harness.hpp"
#include <arpa/inet.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
int rucux_snprintf(char*, size_t, const char*, ...);
int rucux_sscanf(const char*, const char*, ...);
long rucux_strtol(const char*, char**, int);
unsigned long rucux_strtoul(const char*, char**, int);
double rucux_strtod(const char*, char**);
int rucux_inet_pton(int, const char*, void*);
const char* rucux_inet_ntop(int, const void*, char*, socklen_t);
}

TEST(strtol_decimal) {
    ASSERT_EQ(rucux_strtol("42", nullptr, 10), 42L);
    ASSERT_EQ(rucux_strtol("-100", nullptr, 10), -100L);
    ASSERT_EQ(rucux_strtol("  123", nullptr, 10), 123L);
    ASSERT_EQ(rucux_strtol("+999", nullptr, 10), 999L);
}

TEST(strtol_hex) {
    ASSERT_EQ(rucux_strtol("0xff", nullptr, 16), 255L);
    ASSERT_EQ(rucux_strtol("FF", nullptr, 16), 255L);
    ASSERT_EQ(rucux_strtol("0xFF", nullptr, 0), 255L);
}

TEST(strtol_octal) {
    ASSERT_EQ(rucux_strtol("077", nullptr, 8), 63L);
    ASSERT_EQ(rucux_strtol("077", nullptr, 0), 63L);
}

TEST(strtol_auto_base) {
    ASSERT_EQ(rucux_strtol("42", nullptr, 0), 42L);
    ASSERT_EQ(rucux_strtol("0x1F", nullptr, 0), 31L);
    ASSERT_EQ(rucux_strtol("010", nullptr, 0), 8L);
}

TEST(strtol_endptr) {
    char* end;
    ASSERT_EQ(rucux_strtol("123abc", &end, 10), 123L);
    ASSERT_EQ(*end, 'a');
}

TEST(strtoul_basic) {
    ASSERT_EQ(rucux_strtoul("4294967295", nullptr, 10), 4294967295UL);
    ASSERT_EQ(rucux_strtoul("0", nullptr, 10), 0UL);
}

TEST(strtod_basic) {
    double d = rucux_strtod("3.14", nullptr);
    ASSERT(d > 3.13 && d < 3.15);
}

TEST(strtod_negative) {
    double d = rucux_strtod("-2.5", nullptr);
    ASSERT(d > -2.6 && d < -2.4);
}

TEST(strtod_exponent) {
    double d = rucux_strtod("1.5e2", nullptr);
    ASSERT(d > 149.0 && d < 151.0);
}

TEST(strtod_endptr) {
    char* end;
    rucux_strtod("3.14xyz", &end);
    ASSERT_EQ(*end, 'x');
}

TEST(snprintf_zero_capacity) {
    char sentinel = 'X';
    ASSERT_EQ(rucux_snprintf(&sentinel, 0, "%s %d", "abc", 42), 6);
    ASSERT_EQ(sentinel, 'X');
    ASSERT_EQ(rucux_snprintf(nullptr, 0, "%s", "abc"), 3);
}

// Test sscanf
TEST(sscanf_int) {
    int a = 0, b = 0;
    int n = rucux_sscanf("42 -7", "%d %d", &a, &b);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(a, 42);
    ASSERT_EQ(b, -7);
}

TEST(sscanf_hex) {
    unsigned int v = 0;
    int n = rucux_sscanf("0xff", "%x", &v);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(v, 0xFFU);
}

TEST(sscanf_string) {
    char buf[64] = {};
    int n = rucux_sscanf("hello world", "%s", buf);
    ASSERT_EQ(n, 1);
    ASSERT(strcmp(buf, "hello") == 0);
}

TEST(sscanf_mixed) {
    int port = 0;
    char host[64] = {};
    int n = rucux_sscanf("host 8080", "%s %d", host, &port);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(port, 8080);
    ASSERT(strcmp(host, "host") == 0);
}

// Test snprintf patterns used by rtorrent
TEST(snprintf_basic_formats) {
    char buf[256];

    rucux_snprintf(buf, sizeof(buf), "%d", 42);
    ASSERT(strcmp(buf, "42") == 0);

    rucux_snprintf(buf, sizeof(buf), "%u", 42u);
    ASSERT(strcmp(buf, "42") == 0);

    rucux_snprintf(buf, sizeof(buf), "%x", 255);
    ASSERT(strcmp(buf, "ff") == 0);

    rucux_snprintf(buf, sizeof(buf), "%X", 255);
    ASSERT(strcmp(buf, "FF") == 0);

    rucux_snprintf(buf, sizeof(buf), "%s", "hello");
    ASSERT(strcmp(buf, "hello") == 0);

    rucux_snprintf(buf, sizeof(buf), "%c", 'A');
    ASSERT(strcmp(buf, "A") == 0);

    rucux_snprintf(buf, sizeof(buf), "%%");
    ASSERT(strcmp(buf, "%") == 0);
}

TEST(snprintf_long_formats) {
    char buf[256];

    rucux_snprintf(buf, sizeof(buf), "%ld", 1234567890L);
    ASSERT(strcmp(buf, "1234567890") == 0);

    rucux_snprintf(buf, sizeof(buf), "%lu", 4000000000UL);
    ASSERT(strcmp(buf, "4000000000") == 0);
}

TEST(snprintf_padding) {
    char buf[256];

    rucux_snprintf(buf, sizeof(buf), "%10d", 42);
    ASSERT(strcmp(buf, "        42") == 0);

    rucux_snprintf(buf, sizeof(buf), "%-10d|", 42);
    ASSERT(strcmp(buf, "42        |") == 0);

    rucux_snprintf(buf, sizeof(buf), "%08x", 255);
    ASSERT(strcmp(buf, "000000ff") == 0);
}

TEST(snprintf_float) {
    char buf[256];

    rucux_snprintf(buf, sizeof(buf), "%.2f", 3.14);
    ASSERT(strcmp(buf, "3.14") == 0);

    rucux_snprintf(buf, sizeof(buf), "%f", 0.0);
    ASSERT(strcmp(buf, "0.000000") == 0);

    rucux_snprintf(buf, sizeof(buf), "%.1f", -2.5);
    ASSERT(strcmp(buf, "-2.5") == 0);
}

TEST(snprintf_truncation) {
    char buf[8];
    int ret = rucux_snprintf(buf, sizeof(buf), "hello world");
    ASSERT(strcmp(buf, "hello w") == 0); // Truncated at 7 + null
    ASSERT_EQ(ret, 11);                  // Would have written 11 chars
}

// Test inet functions — basic validation
TEST(inet_pton_v4) {
    // Using host inet_pton as reference
    struct {
        unsigned int s_addr;
    } addr;
    int ret = rucux_inet_pton(AF_INET, "10.0.0.1", &addr);
    ASSERT_EQ(ret, 1);
    // 10.0.0.1 in network byte order = 0x0100000a
    unsigned char* b = (unsigned char*)&addr.s_addr;
    ASSERT_EQ(b[0], 10);
    ASSERT_EQ(b[1], 0);
    ASSERT_EQ(b[2], 0);
    ASSERT_EQ(b[3], 1);
}

TEST(inet_ntop_v4) {
    unsigned char addr[] = {192, 168, 1, 100};
    char buf[16];
    const char* ret = rucux_inet_ntop(AF_INET, addr, buf, sizeof(buf));
    ASSERT(ret != nullptr);
    ASSERT(strcmp(buf, "192.168.1.100") == 0);
}

int main() {
    return RUN_ALL_TESTS("libc Completion Tests");
}
