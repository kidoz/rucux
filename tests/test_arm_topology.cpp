// SPDX-License-Identifier: MIT
#include "test_harness.hpp"
#include <arch/armv7/topology.hpp>
#include <array>
#include <string>
#include <vector>
namespace {
using bytes = std::vector<uint8_t>;
void put(bytes& b, uint32_t v) {
    for (int n = 24; n >= 0; n -= 8)
        b.push_back(static_cast<uint8_t>(v >> n));
}
void set(bytes& b, size_t at, uint32_t v) {
    for (int n = 3; n >= 0; --n) {
        b[at + n] = static_cast<uint8_t>(v);
        v >>= 8;
    }
}
bytes string(const char* s) {
    return bytes(s, s + std::char_traits<char>::length(s) + 1);
}
struct fixture {
    bytes structure, names;
    void begin(const char* name) {
        put(structure, 1);
        auto s = string(name);
        structure.insert(structure.end(), s.begin(), s.end());
        pad();
    }
    void end() { put(structure, 2); }
    void pad() {
        while (structure.size() % 4)
            structure.push_back(0);
    }
    void prop(const char* name, bytes value) {
        put(structure, 3);
        put(structure, value.size());
        put(structure, names.size());
        auto key = string(name);
        names.insert(names.end(), key.begin(), key.end());
        structure.insert(structure.end(), value.begin(), value.end());
        pad();
    }
    void integer(const char* name, uint32_t value) {
        bytes b;
        put(b, value);
        prop(name, b);
    }
    bytes make(unsigned count = 4, unsigned cells = 1, const char* conduit = "hvc", bool duplicate = false,
               bool disabled = false) {
        begin("");
        begin("cpus");
        integer("#address-cells", cells);
        for (unsigned n = 0; n < count; ++n) {
            begin("cpu@0");
            prop("device_type", string("cpu"));
            bytes reg;
            if (cells == 2) put(reg, 0);
            put(reg, duplicate ? 0 : n);
            prop("reg", reg);
            if (disabled && n == count - 1) prop("status", string("disabled"));
            end();
        }
        end();
        begin("psci");
        prop("compatible", string("arm,psci-0.2"));
        prop("method", string(conduit));
        end();
        end();
        put(structure, 9);
        bytes result(40);
        set(result, 0, 0xd00dfeed);
        set(result, 4, 40 + structure.size() + names.size());
        set(result, 8, 40);
        set(result, 12, 40 + structure.size());
        set(result, 20, 17);
        set(result, 24, 16);
        set(result, 32, names.size());
        set(result, 36, structure.size());
        result.insert(result.end(), structure.begin(), structure.end());
        result.insert(result.end(), names.begin(), names.end());
        return result;
    }
};
bool parse(const bytes& b, arch::armv7::cpu_topology& out) {
    return arch::armv7::parse_cpu_topology(b.data(), b.size(), out);
}
} // namespace
TEST(enabled_affinities_and_hvc_conduit_are_discovered) {
    fixture f;
    auto b = f.make();
    arch::armv7::cpu_topology out;
    ASSERT(parse(b, out));
    ASSERT_EQ(out.count, 4U);
    ASSERT(out.psci && out.hvc);
    ASSERT_EQ(out.blob_size, b.size());
    for (unsigned i = 0; i < 4; ++i)
        ASSERT_EQ(out.mpidrs[i], i);
}
TEST(two_cell_affinities_and_smc_are_supported) {
    fixture f;
    auto b = f.make(2, 2, "smc");
    arch::armv7::cpu_topology out;
    ASSERT(parse(b, out));
    ASSERT_EQ(out.count, 2U);
    ASSERT(out.psci && !out.hvc);
}
TEST(disabled_cpus_do_not_become_startup_targets) {
    fixture f;
    auto b = f.make(4, 1, "hvc", false, true);
    arch::armv7::cpu_topology out;
    ASSERT(parse(b, out));
    ASSERT_EQ(out.count, 3U);
}
TEST(duplicate_affinities_unknown_conduits_and_excess_cpu_count_are_rejected) {
    for (auto b : {fixture{}.make(2, 1, "hvc", true), fixture{}.make(2, 1, "bad"), fixture{}.make(9)}) {
        arch::armv7::cpu_topology out;
        ASSERT(!parse(b, out));
        ASSERT_EQ(out.count, 0U);
    }
}
TEST(every_truncated_prefix_is_rejected_without_partial_results) {
    auto b = fixture{}.make();
    for (size_t size = 0; size < b.size(); ++size) {
        arch::armv7::cpu_topology out;
        out.count = 77;
        ASSERT(!arch::armv7::parse_cpu_topology(b.data(), size, out));
        ASSERT_EQ(out.count, 0U);
    }
}
TEST(header_offsets_and_lengths_are_bounded) {
    auto original = fixture{}.make();
    for (const auto at : {4U, 8U, 12U, 32U, 36U}) {
        auto b = original;
        set(b, at, 0xfffffff0);
        arch::armv7::cpu_topology out;
        ASSERT(!parse(b, out));
    }
    auto b = original;
    set(b, 12, 40);
    arch::armv7::cpu_topology out;
    ASSERT(!parse(b, out));
}
TEST(property_lengths_string_offsets_and_node_names_are_bounded) {
    fixture f;
    auto b = f.make();
    arch::armv7::cpu_topology out;
    // First property follows root and cpus names: token at structure + 20.
    auto length = b;
    set(length, 40 + 24, 0xfffffff0);
    ASSERT(!parse(length, out));
    auto name = b;
    set(name, 40 + 28, 0xfffffff0);
    ASSERT(!parse(name, out));
    auto node = b;
    for (size_t at = 44; at < 40 + f.structure.size(); ++at)
        node[at] = 0xff;
    ASSERT(!parse(node, out));
}
TEST(missing_end_tokens_and_non_cpu_nodes_cannot_supply_targets) {
    fixture f;
    auto b = f.make();
    set(b, 40 + f.structure.size() - 4, 4);
    arch::armv7::cpu_topology out;
    ASSERT(!parse(b, out));
    auto empty = fixture{}.make(0);
    ASSERT(!parse(empty, out));
}
int main() {
    return RUN_ALL_TESTS("Bounded ARM CPU topology discovery");
}
