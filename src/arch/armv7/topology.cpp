// SPDX-License-Identifier: MIT
#include <arch/armv7/topology.hpp>
namespace arch::armv7 {
namespace {
uint32_t word(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
}
bool equal(const uint8_t* p, size_t n, const char* s) {
    size_t i = 0;
    while (i < n && s[i] && p[i] == static_cast<uint8_t>(s[i]))
        ++i;
    return i < n && !s[i] && !p[i];
}
bool prefix(const uint8_t* p, size_t n, const char* s) {
    size_t i = 0;
    while (s[i]) {
        if (i >= n || p[i] != static_cast<uint8_t>(s[i])) return false;
        ++i;
    }
    return true;
}
struct node {
    enum kind { other, cpus, cpu, psci_node } type = other;
    uint32_t cells = 1, id = 0;
    bool reg = false, enabled = true, device_cpu = false, compatible = false, method = false, hvc = false;
};
} // namespace
bool parse_cpu_topology(const void* blob, size_t available, cpu_topology& result) noexcept {
    result = {};
    if (!blob || available < 40) return false;
    const auto* bytes = static_cast<const uint8_t*>(blob);
    if (word(bytes) != 0xd00dfeed) return false;
    const auto total = word(bytes + 4), so = word(bytes + 8), no = word(bytes + 12);
    const auto ns = word(bytes + 32), ss = word(bytes + 36);
    if (total < 40 || total > available || total > 0x100000 || so < 40 || no < 40 || (so & 3) || so > total ||
        ss > total - so || no > total || ns > total - no || (so < no + ns && no < so + ss))
        return false;
    const auto* p = bytes + so;
    const auto* end = p + ss;
    node stack[16]{};
    unsigned depth = 0;
    cpu_topology parsed{};
    while (end - p >= 4) {
        const auto token = word(p);
        p += 4;
        if (token == 1) {
            if (depth == 16) return false;
            size_t length = 0;
            while (p + length < end && p[length])
                ++length;
            if (p + length == end) return false;
            auto& current = stack[depth];
            current = {};
            if (depth == 1 && equal(p, length + 1, "cpus"))
                current.type = node::cpus;
            else if (depth && stack[depth - 1].type == node::cpus &&
                     (prefix(p, length, "cpu@") || equal(p, length + 1, "cpu"))) {
                current.type = node::cpu;
                current.cells = stack[depth - 1].cells;
            } else if (depth == 1 && (equal(p, length + 1, "psci") || prefix(p, length, "psci@")))
                current.type = node::psci_node;
            const auto skip = (length + 1 + 3) & ~size_t{3};
            if (skip > static_cast<size_t>(end - p)) return false;
            p += skip;
            ++depth;
        } else if (token == 2) {
            if (!depth) return false;
            const auto& current = stack[--depth];
            if (current.type == node::cpu && current.enabled) {
                if (!current.reg || !current.device_cpu || parsed.count == MAX_CPUS) return false;
                for (uint32_t i = 0; i < parsed.count; ++i)
                    if (parsed.mpidrs[i] == current.id) return false;
                parsed.mpidrs[parsed.count++] = current.id;
            }
            if (current.type == node::psci_node && current.compatible && current.method) {
                if (parsed.psci) return false;
                parsed.psci = true;
                parsed.hvc = current.hvc;
            }
        } else if (token == 3) {
            if (!depth || end - p < 8) return false;
            const auto length = word(p), offset = word(p + 4);
            p += 8;
            if (length > static_cast<size_t>(end - p) || offset >= ns) return false;
            const auto* name = bytes + no + offset;
            size_t named = 0;
            while (named < ns - offset && name[named])
                ++named;
            if (named == ns - offset) return false;
            auto& current = stack[depth - 1];
            if (equal(name, named + 1, "compatible")) {
                if (!length || p[length - 1]) return false;
                size_t at = 0;
                while (at < length) {
                    if (equal(p + at, length - at, "arm,psci-0.2") || equal(p + at, length - at, "arm,psci-1.0"))
                        current.compatible = true;
                    while (at < length && p[at])
                        ++at;
                    ++at;
                }
            } else if (current.type == node::cpus && equal(name, named + 1, "#address-cells")) {
                if (length != 4 || (word(p) != 1 && word(p) != 2)) return false;
                current.cells = word(p);
            } else if (current.type == node::cpu) {
                if (equal(name, named + 1, "reg")) {
                    if (length != current.cells * 4 || (current.cells == 2 && word(p))) return false;
                    current.id = word(p + length - 4);
                    if (current.id & 0xFF000000U) return false;
                    current.reg = true;
                } else if (equal(name, named + 1, "device_type"))
                    current.device_cpu = equal(p, length, "cpu");
                else if (equal(name, named + 1, "status")) {
                    if (!length || p[length - 1]) return false;
                    current.enabled = equal(p, length, "okay") || equal(p, length, "ok");
                }
            } else if (current.type == node::psci_node && equal(name, named + 1, "method")) {
                if (!equal(p, length, "hvc") && !equal(p, length, "smc")) return false;
                current.method = true;
                current.hvc = equal(p, length, "hvc");
            }
            const auto skip = (static_cast<size_t>(length) + 3) & ~size_t{3};
            if (skip > static_cast<size_t>(end - p)) return false;
            p += skip;
        } else if (token == 9) {
            if (depth || !parsed.count || (parsed.count > 1 && !parsed.psci)) return false;
            parsed.blob_size = total;
            result = parsed;
            return true;
        } else if (token != 4)
            return false;
    }
    return false;
}
} // namespace arch::armv7
